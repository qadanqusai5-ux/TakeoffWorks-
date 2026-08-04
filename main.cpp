#include <QApplication>
#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QComboBox>
#include <QTreeWidget>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QDebug>
#include <memory>

// ==========================================
// 1. تضمين مكتبة PDFium (يجب تثبيتها)
// ==========================================
#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_text.h>
#include <fpdf_edit.h>
#include <fpdf_formfill.h>

// ==========================================
// 2. كلاس إدارة ملفات PDF
// ==========================================
class PdfDocument {
public:
    PdfDocument() : m_doc(nullptr) {
        static bool initialized = false;
        if (!initialized) {
            FPDF_InitLibrary();
            initialized = true;
        }
    }
    ~PdfDocument() { close(); }

    bool load(const QString &filePath) {
        if (m_doc) close();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        QByteArray data = file.readAll();
        m_doc = FPDF_LoadMemDocument(data.constData(), data.size(), nullptr);
        return m_doc != nullptr;
    }

    void close() {
        if (m_doc) { FPDF_CloseDocument(m_doc); m_doc = nullptr; }
    }
    FPDF_DOCUMENT handle() const { return m_doc; }
    int pageCount() const { return m_doc ? FPDF_GetPageCount(m_doc) : 0; }
    QSizeF pageSize(int pageIndex) const {
        if (!m_doc) return QSizeF();
        double w=0, h=0;
        FPDF_GetPageSizeByIndexF(m_doc, pageIndex, &w, &h);
        return QSizeF(w, h);
    }

private:
    FPDF_DOCUMENT m_doc;
};

// ==========================================
// 3. كلاس عرض الصفحة
// ==========================================
class PdfPageRenderer {
public:
    PdfPageRenderer(PdfDocument *doc) : m_doc(doc) {}

    QImage renderPage(int pageIndex, const QSize &size) {
        if (!m_doc || !m_doc->handle()) return QImage();
        FPDF_PAGE page = FPDF_LoadPage(m_doc->handle(), pageIndex);
        if (!page) return QImage();

        double width = FPDF_GetPageWidthF(page);
        double height = FPDF_GetPageHeightF(page);
        double scaleX = size.width() / width;
        double scaleY = size.height() / height;
        double scale = qMin(scaleX, scaleY);

        int bitmapWidth = static_cast<int>(width * scale);
        int bitmapHeight = static_cast<int>(height * scale);
        FPDF_BITMAP bitmap = FPDFBitmap_Create(bitmapWidth, bitmapHeight, 0);
        FPDFBitmap_FillRect(bitmap, 0, 0, bitmapWidth, bitmapHeight, 0xFFFFFFFF);
        FPDF_RenderPageBitmap(bitmap, page, 0, 0, bitmapWidth, bitmapHeight, 0, 0);

        unsigned char *buffer = FPDFBitmap_GetBuffer(bitmap);
        int stride = FPDFBitmap_GetStride(bitmap);
        QImage img(buffer, bitmapWidth, bitmapHeight, stride, QImage::Format_ARGB32);
        QImage result = img.copy(); // نسخة عميقة

        FPDFBitmap_Destroy(bitmap);
        FPDF_ClosePage(page);
        return result;
    }

private:
    PdfDocument *m_doc;
};

// ==========================================
// 4. كلاس التحديد والتظليل (Selection Overlay)
// ==========================================
class SelectionOverlay : public QObject {
    Q_OBJECT
public:
    SelectionOverlay(QGraphicsView *view, QObject *parent = nullptr)
        : QObject(parent), m_view(view), m_scene(view->scene()), m_rectItem(nullptr), m_isActive(false) {}

    void start(const QPointF &pos) {
        if (!m_scene) return;
        clear();
        m_startPoint = pos;
        m_endPoint = pos;
        m_isActive = true;
        m_rectItem = new QGraphicsRectItem(QRectF(pos, pos));
        m_rectItem->setPen(QPen(Qt::red, 1, Qt::DashLine));
        m_rectItem->setBrush(QBrush(QColor(255, 0, 0, 20)));
        m_scene->addItem(m_rectItem);
    }

    void update(const QPointF &pos) {
        if (!m_isActive || !m_rectItem) return;
        m_endPoint = pos;
        m_rectItem->setRect(QRectF(m_startPoint, m_endPoint).normalized());
        emit selectionChanged(m_rectItem->rect());
    }

    void finish() {
        if (!m_isActive) return;
        m_isActive = false;
        m_selectionRect = QRectF(m_startPoint, m_endPoint).normalized();
        emit selectionFinished(m_selectionRect);
    }

    void clear() {
        if (m_rectItem) { m_scene->removeItem(m_rectItem); delete m_rectItem; m_rectItem = nullptr; }
        m_isActive = false;
    }
    QRectF selectionRect() const { return m_selectionRect; }

signals:
    void selectionChanged(const QRectF &rect);
    void selectionFinished(const QRectF &rect);

private:
    QGraphicsView *m_view;
    QGraphicsScene *m_scene;
    QGraphicsRectItem *m_rectItem;
    QPointF m_startPoint, m_endPoint;
    bool m_isActive;
    QRectF m_selectionRect;
};

// ==========================================
// 5. النافذة الرئيسية (الواجهة الرسومية)
// ==========================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent), m_currentPage(0) {
        setWindowTitle("TakeoffWorks Native - PDF Inspector");
        setMinimumSize(1000, 700);

        // تهيئة المحرك
        m_pdfDoc = std::make_unique<PdfDocument>();
        m_renderer = std::make_unique<PdfPageRenderer>(m_pdfDoc.get());

        // الواجهة الرسومية
        QWidget *central = new QWidget(this);
        setCentralWidget(central);
        QVBoxLayout *mainLayout = new QVBoxLayout(central);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        QSplitter *splitter = new QSplitter(Qt::Horizontal, central);
        mainLayout->addWidget(splitter);

        // اللوحة اليسرى (الفحص)
        QWidget *leftPanel = new QWidget(splitter);
        QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
        m_inspectorTree = new QTreeWidget(leftPanel);
        m_inspectorTree->setHeaderLabels({"Property", "Value"});
        m_inspectorTree->header()->setSectionResizeMode(QHeaderView::Stretch);
        leftLayout->addWidget(m_inspectorTree);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        QPushButton *inspectBtn = new QPushButton("Inspect Area", leftPanel);
        QPushButton *exportBtn = new QPushButton("Export JSON", leftPanel);
        btnLayout->addWidget(inspectBtn);
        btnLayout->addWidget(exportBtn);
        leftLayout->addLayout(btnLayout);

        // اللوحة اليمنى (عرض الـ PDF)
        QWidget *rightPanel = new QWidget(splitter);
        QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
        rightLayout->setContentsMargins(0, 0, 0, 0);

        m_scene = new QGraphicsScene(rightPanel);
        m_view = new QGraphicsView(m_scene, rightPanel);
        m_view->setDragMode(QGraphicsView::NoDrag);
        m_view->setRenderHint(QPainter::Antialiasing);
        rightLayout->addWidget(m_view);

        QHBoxLayout *navLayout = new QHBoxLayout();
        navLayout->addWidget(new QLabel("Page:"));
        m_pageCombo = new QComboBox();
        navLayout->addWidget(m_pageCombo);
        navLayout->addStretch();
        rightLayout->addLayout(navLayout);

        splitter->setSizes({300, 800});

        // شريط الأدوات
        QToolBar *toolbar = addToolBar("Main");
        toolbar->addAction("Open PDF", this, &MainWindow::onOpenPdf);
        QAction *selectAct = toolbar->addAction("Select Area");
        selectAct->setCheckable(true);
        connect(selectAct, &QAction::toggled, this, &MainWindow::onSelectDrawingArea);

        // شريط الحالة
        m_statusLabel = new QLabel("Ready");
        statusBar()->addWidget(m_statusLabel);

        // توصيل الإشارات
        connect(m_pageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onPageChanged);
        connect(inspectBtn, &QPushButton::clicked, this, &MainWindow::onInspectArea);
        connect(exportBtn, &QPushButton::clicked, this, &MainWindow::onExportJson);

        // تهيئة الطبقة المسؤولة عن التحديد
        m_selectionOverlay = std::make_unique<SelectionOverlay>(m_view, this);
        connect(m_selectionOverlay.get(), &SelectionOverlay::selectionFinished, this, &MainWindow::onSelectionFinished);
    }

private slots:
    void onOpenPdf() {
        QString path = QFileDialog::getOpenFileName(this, "Open PDF", "", "PDF Files (*.pdf)");
        if (path.isEmpty()) return;
        if (!m_pdfDoc->load(path)) {
            QMessageBox::critical(this, "Error", "Failed to load PDF.");
            return;
        }
        m_pageCombo->clear();
        for (int i = 0; i < m_pdfDoc->pageCount(); ++i) m_pageCombo->addItem(QString("Page %1").arg(i+1));
        onPageChanged(0);
        m_statusLabel->setText(QString("Loaded: %1").arg(QFileInfo(path).fileName()));
    }

    void onSelectDrawingArea(bool checked) {
        m_isSelecting = checked;
        m_view->setCursor(checked ? Qt::CrossCursor : Qt::ArrowCursor);
        m_statusLabel->setText(checked ? "Click and drag to select area." : "Selection cancelled.");
        if (!checked && m_selectionOverlay) m_selectionOverlay->clear();
    }

    void onPageChanged(int pageIndex) {
        if (!m_pdfDoc->handle() || pageIndex < 0) return;
        m_currentPage = pageIndex;
        QImage img = m_renderer->renderPage(pageIndex, m_view->viewport()->size());
        if (img.isNull()) { m_statusLabel->setText("Render failed."); return; }

        m_scene->clear();
        m_scene->addPixmap(QPixmap::fromImage(img));
        m_scene->setSceneRect(img.rect());
        m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
        m_statusLabel->setText(QString("Page %1").arg(pageIndex + 1));
    }

    void onSelectionFinished(const QRectF &rect) {
        m_selectedRect = rect;
        m_statusLabel->setText(QString("Selected: (%1, %2) %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()));
        m_isSelecting = false;
        m_view->setCursor(Qt::ArrowCursor);
        onInspectArea();
    }

    void onInspectArea() {
        if (m_selectedRect.isEmpty()) { QMessageBox::information(this, "Info", "Select an area first."); return; }
        
        QRectF sceneRect = m_scene->sceneRect();
        QSizeF pageSize = m_pdfDoc->pageSize(m_currentPage);
        double scaleX = pageSize.width() / sceneRect.width();
        double scaleY = pageSize.height() / sceneRect.height();
        double scale = qMin(scaleX, scaleY);
        QRectF pdfRect(m_selectedRect.x() * scale, m_selectedRect.y() * scale,
                       m_selectedRect.width() * scale, m_selectedRect.height() * scale);

        FPDF_PAGE page = FPDF_LoadPage(m_pdfDoc->handle(), m_currentPage);
        if (!page) return;

        // استخراج النصوص
        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        int count = FPDFText_CountChars(textPage);
        QString text;
        if (count > 0) {
            QVector<unsigned short> buf(count + 1);
            FPDFText_GetText(textPage, 0, count, buf.data());
            text = QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.data()));
        }
        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);

        m_inspectorTree->clear();
        QTreeWidgetItem *root = new QTreeWidgetItem(m_inspectorTree);
        root->setText(0, "Objects Found");
        root->setText(1, "Details");

        if (!text.isEmpty()) {
            QTreeWidgetItem *item = new QTreeWidgetItem(root);
            item->setText(0, "Text Content");
            item->setText(1, text.trimmed());
        }

        // إضافة بعض المعلومات الأساسية
        QTreeWidgetItem *rectItem = new QTreeWidgetItem(root);
        rectItem->setText(0, "Area (PDF points)");
        rectItem->setText(1, QString("(%1, %2) %3x%4").arg(pdfRect.x()).arg(pdfRect.y()).arg(pdfRect.width()).arg(pdfRect.height()));

        root->setExpanded(true);
    }

    void onExportJson() {
        if (m_inspectorTree->topLevelItemCount() == 0) {
            QMessageBox::information(this, "Info", "No objects to export.");
            return;
        }
        QString path = QFileDialog::getSaveFileName(this, "Export JSON", "", "JSON (*.json)");
        if (path.isEmpty()) return;

        QJsonObject root;
        QJsonArray items;
        QTreeWidgetItem *top = m_inspectorTree->topLevelItem(0);
        for (int i=0; i<top->childCount(); ++i) {
            QTreeWidgetItem *child = top->child(i);
            QJsonObject obj;
            obj["name"] = child->text(0);
            obj["value"] = child->text(1);
            items.append(obj);
        }
        root["objects"] = items;

        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson());
            file.close();
            m_statusLabel->setText("Exported to " + path);
        }
    }

private:
    std::unique_ptr<PdfDocument> m_pdfDoc;
    std::unique_ptr<PdfPageRenderer> m_renderer;
    std::unique_ptr<SelectionOverlay> m_selectionOverlay;
    QGraphicsView *m_view;
    QGraphicsScene *m_scene;
    QLabel *m_statusLabel;
    QComboBox *m_pageCombo;
    QTreeWidget *m_inspectorTree;
    int m_currentPage;
    bool m_isSelecting = false;
    QRectF m_selectedRect;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}