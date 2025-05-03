#include "../network/network.hpp"

#include <QApplication>
#include <QMainWindow>
#include <QFileSystemModel>
#include <QTreeView>
#include <QListView>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>
#include <QLineEdit>
#include <QToolBar>
#include <QAction>
#include <QIcon>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QMetaObject>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <vector>
#include <string>
#include <memory>
#include <future>
#include <functional>
#include <chrono>
#include <utility>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void on_treeView_clicked(const QModelIndex &index);

    void on_listView_doubleClicked(const QModelIndex &index);

    void goUp();

    void on_pathLineEdit_returnPressed();

    void updateUiWithPathInfo(const QString path, bool success, const QString errorMsg = "");

    void handleOpenFileResult(const QString filePath, bool success);

private:
    void setupUi();

    void startNavigateToPath(const QString &path);

    pair<bool, QString> performNavigation(QString path);

    void startOpenFile(const QString &filePath);

    QFileSystemModel *treeViewModel = nullptr;
    QFileSystemModel *listViewModel = nullptr;

    QTreeView *treeView = nullptr;
    QListView *listView = nullptr;
    QLineEdit *pathLineEdit = nullptr;
    QSplitter *splitter = nullptr;
    QToolBar *toolBar = nullptr;
    QAction *upAction = nullptr;

    future<void> navigationFuture;
    future<void> openFileFuture;
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setupUi();
    log_write_regular_information("MainWindow created successfully.");
}

void MainWindow::setupUi()
{
    treeViewModel = new QFileSystemModel(this);
    listViewModel = new QFileSystemModel(this);

    treeViewModel->setRootPath("");
    treeViewModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    log_write_regular_information("treeViewModel initialized. Filter: AllDirs.");

    listViewModel->setRootPath("");
    listViewModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    log_write_regular_information("listViewModel initialized. Filter: AllEntries.");

    treeView = new QTreeView(this);
    treeView->setModel(treeViewModel);
    for (int i = 1; i < treeViewModel->columnCount(); ++i)
        treeView->hideColumn(i);
    treeView->setRootIndex(treeViewModel->index(treeViewModel->rootPath()));
    treeView->setSortingEnabled(true);

    listView = new QListView(this);
    listView->setModel(listViewModel);
    listView->setViewMode(QListView::IconMode);
    listView->setGridSize(QSize(80, 80));
    listView->setResizeMode(QListView::Adjust);
    listView->setMovement(QListView::Static);
    listView->setUniformItemSizes(true);

    pathLineEdit = new QLineEdit(this);

    splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(treeView);
    splitter->addWidget(listView);
    splitter->setStretchFactor(1, 2);

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->addWidget(pathLineEdit);
    mainLayout->addWidget(splitter);
    setCentralWidget(centralWidget);

    toolBar = addToolBar("Navigation");
    upAction = new QAction("Up", this);
    toolBar->addAction(upAction);
    log_write_regular_information("UI components created and laid out.");

    connect(treeView, &QTreeView::clicked, this, &MainWindow::on_treeView_clicked);
    connect(listView, &QListView::doubleClicked, this, &MainWindow::on_listView_doubleClicked);
    connect(upAction, &QAction::triggered, this, &MainWindow::goUp);
    connect(pathLineEdit, &QLineEdit::returnPressed, this, &MainWindow::on_pathLineEdit_returnPressed);
    log_write_regular_information("Signal-slot connections established.");

    setWindowTitle("Klee酱的文件浏览器 ✨ (v2.2 - 文件显示修复)");
    resize(800, 600);

    QString initialPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (initialPath.isEmpty() or !QDir(initialPath).exists())
    {
        log_write_warning_information("Home location path is invalid or empty. Falling back to current path.");
        initialPath = QDir::currentPath();
    }
    if (initialPath.isEmpty())
    {
        log_write_error_information("Initial path is still empty after fallback! Using root path.");
        initialPath = QDir::rootPath();
    }

    log_write_regular_information("Starting initial navigation to: " + initialPath.toStdString());
    startNavigateToPath(initialPath);
}

void MainWindow::on_treeView_clicked(const QModelIndex &index)
{
    if (!index.isValid())
    {
        log_write_warning_information("Invalid index clicked in tree view.");
        return;
    }
    QString path = treeViewModel->filePath(index);
    if (path.isEmpty())
    {
        log_write_warning_information("Empty path obtained from tree view click. Index might represent a drive or special item.");
        return;
    }
    log_write_regular_information("Tree view item clicked: " + path.toStdString());
    startNavigateToPath(path);
}

void MainWindow::on_listView_doubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
    {
        log_write_warning_information("Invalid index double-clicked in list view.");
        return;
    }

    QFileInfo fileInfo = listViewModel->fileInfo(index);
    QString absolutePath = fileInfo.absoluteFilePath();
    log_write_regular_information("List view item double-clicked: " + absolutePath.toStdString());

    if (fileInfo.isDir())
        startNavigateToPath(absolutePath);
    else if (fileInfo.isFile())
        startOpenFile(absolutePath);
    else
        log_write_warning_information("Double-clicked item is neither a file nor a directory: " + absolutePath.toStdString());
}

void MainWindow::goUp()
{
    QString currentPath = listViewModel->filePath(listView->rootIndex());
    if (currentPath.isEmpty())
    {
        log_write_warning_information("Cannot go up, current list view path is empty or invalid.");
        return;
    }
    log_write_regular_information("Attempting to go up from: " + currentPath.toStdString());

    QDir dir(currentPath);
    if (dir.cdUp())
        startNavigateToPath(dir.absolutePath());
    else
        log_write_warning_information("Cannot go up from path: " + currentPath.toStdString());
}

void MainWindow::on_pathLineEdit_returnPressed()
{
    QString path = pathLineEdit->text();
    log_write_regular_information("Path entered in address bar: " + path.toStdString());
    startNavigateToPath(path);
}

void MainWindow::startNavigateToPath(const QString &path)
{
    if (path.isEmpty())
    {
        log_write_error_information("Navigation requested with empty path. Aborting.");
        return;
    }

    if (navigationFuture.valid() and navigationFuture.wait_for(chrono::seconds(0)) == future_status::timeout)
    {
        log_write_warning_information("Previous navigation task still running, starting new one.");
        
    }

    log_write_regular_information("Enqueueing navigation task for path: " + path.toStdString());
    navigationFuture = ThreadPool::instance().enqueue(0, [this, path]()
                                                      {
        auto result = performNavigation(path);
        bool success = result.first;
        QString errorMsg = result.second;

        QMetaObject::invokeMethod(this, "updateUiWithPathInfo", Qt::QueuedConnection,
                                  Q_ARG(QString, path),
                                  Q_ARG(bool, success),
                                  Q_ARG(QString, errorMsg)); });
}

pair<bool, QString> MainWindow::performNavigation(QString path)
{
    log_write_regular_information("Background thread performing navigation to: " + path.toStdString());
    QFileInfo pathInfo(path);
    if (!pathInfo.exists())
    {
        string errorMsg = "Navigation failed: Path does not exist or is inaccessible: " + path.toStdString();
        log_write_error_information(errorMsg);
        return {false, QString::fromStdString(errorMsg)};
    }
    // You can add other checks here, such as whether it is a directory (if you need to restrict the navigation target)
    // if (!pathInfo.isDir()) { ... }
    log_write_regular_information("Background thread navigation check successful for: " + path.toStdString());
    return {true, ""};
}

void MainWindow::updateUiWithPathInfo(const QString path, bool success, const QString errorMsg)
{
    log_write_regular_information("Main thread received navigation result for: " + path.toStdString() + ", Success: " + (success ? "true" : "false"));
    if (success)
    {
        QModelIndex newRootIndex = listViewModel->index(path);
        if (newRootIndex.isValid())
        {
            listView->setRootIndex(newRootIndex);
            log_write_regular_information("List view root index updated to: " + path.toStdString() + ". Files and folders should be visible.");
        }
        else
        {
            log_write_error_information("Failed to get valid listViewModel index for path: " + path.toStdString());
            QMessageBox::warning(this, "导航错误", "无法在列表模型中找到有效索引：" + path);
        }

        QModelIndex treeIndex = treeViewModel->index(path);
        if (treeIndex.isValid())
        {
            treeView->expand(treeIndex);
            treeView->scrollTo(treeIndex, QAbstractItemView::PositionAtCenter); // 滚动到可见区域
            treeView->selectionModel()->setCurrentIndex(treeIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows); // 选中节点
            log_write_regular_information("Tree view expanded/scrolled/selected (if enabled) to: " + path.toStdString());
        }
        else
        {
            log_write_warning_information("Path not found or not a directory in tree view model: " + path.toStdString());
        }

        pathLineEdit->setText(QDir::toNativeSeparators(path));
        log_write_regular_information("Address bar updated to: " + path.toStdString());

        QDir currentDir(path);
        QModelIndex rootIndexForTree = treeViewModel->index(treeViewModel->rootPath());
        QString rootPathForTree = (rootIndexForTree.isValid()) ? treeViewModel->filePath(rootIndexForTree) : "";
        bool isRoot = (QDir::cleanPath(path) == QDir::cleanPath(rootPathForTree));
        bool canGoUp = currentDir.cdUp();
        upAction->setEnabled(canGoUp);
        log_write_regular_information("Up action enabled state set to: " + string(canGoUp ? "true" : "false"));
    }
    else
    {
        log_write_error_information("UI Update skipped due to navigation failure for path: " + path.toStdString() + ". Error: " + errorMsg.toStdString());
        QMessageBox::warning(this, "路径无效", "无法导航到路径：" + path + "\n" + errorMsg);
        QString previousValidPath = listViewModel->filePath(listView->rootIndex());
        if (!previousValidPath.isEmpty())
        {
            pathLineEdit->setText(QDir::toNativeSeparators(previousValidPath));
            log_write_regular_information("Address bar reverted to previous valid path: " + previousValidPath.toStdString());
        }
        else
        {
            pathLineEdit->setText(QDir::homePath());
            log_write_warning_information("Previous valid path was empty, reverting address bar to home path.");
        }
    }
    log_write_regular_information("UI update process finished for path: " + path.toStdString());
}

void MainWindow::startOpenFile(const QString &filePath)
{
    if (openFileFuture.valid() and openFileFuture.wait_for(chrono::seconds(0)) == future_status::timeout)
        log_write_warning_information("Previous open file task still running, starting new one.");
    log_write_regular_information("Enqueueing open file task for: " + filePath.toStdString());

    openFileFuture = ThreadPool::instance().enqueue(1, [this, filePath]() { 
        log_write_regular_information("Background thread attempting to open file: " + filePath.toStdString());
        bool success = QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        QMetaObject::invokeMethod(this, "handleOpenFileResult", Qt::QueuedConnection,
                                  Q_ARG(QString, filePath),
                                  Q_ARG(bool, success));
    });
}

void MainWindow::handleOpenFileResult(const QString filePath, bool success)
{
    log_write_regular_information("Main thread received open file result for: " + filePath.toStdString() + ", Success: " + (success ? "true" : "false"));
    if (!success)
    {
        string errorMsg = "Failed to open file: No associated application found or an error occurred for " + filePath.toStdString();
        log_write_error_information(errorMsg);
        QMessageBox::warning(this, "无法打开文件", QString::fromStdString(errorMsg));
    }
    else
    {
        log_write_regular_information("File opened successfully (or request sent to OS): " + filePath.toStdString());
    }
}

int runMainWindow(const vector<string> &parameters)
{
    int argc = 1;
    char *argv[] = {(char *)"KleeExplorerApp", nullptr};
    QApplication *app_ptr = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app_ptr)
    {
        char **non_const_argv = const_cast<char **>(argv);
        app_ptr = new QApplication(argc, non_const_argv);
        log_write_regular_information("Created new QApplication instance.");
    }
    else
        log_write_regular_information("Using existing QApplication instance."); 
    QApplication &app = *app_ptr;

    MainWindow mainWindow;

    log_write_regular_information("Received parameters (count: " + to_string(parameters.size()) + ")");
    for (size_t i = 0; i < parameters.size(); ++i)
        log_write_regular_information(" - Param[" + to_string(i) + "]: " + parameters[i]);

    mainWindow.show();
    log_write_regular_information("MainWindow shown. Starting event loop...");

    int exitCode = app.exec();
    log_write_regular_information("Qt event loop finished with exit code: " + to_string(exitCode));

    return exitCode;
}

#include "MainWindow.moc"
