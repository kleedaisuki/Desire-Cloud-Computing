# Simple-K Cloud Executor - 前端项目文档

欢迎来到 Simple-K Cloud Executor 的前端项目！这是一个基于 C++20 和 Qt 6.9.0 构建的现代化图形用户界面 (GUI) 客户端，旨在为用户提供流畅的文件管理体验，并能与 Simple-K Cloud Executor 后端服务器无缝协作，实现文件的远程提交与结果接收。本项目注重用户体验和代码的模块化设计。

## 📝 项目概述 (Project Overview)

Simple-K Cloud Executor 前端是一个跨平台的客户端应用程序，它允许用户：

* 直观地浏览本地文件系统。
* 选择文件并将其安全地发送到后端服务器进行处理（例如，云端编译、远程执行或其他自定义任务）。
* 实时追踪文件传输和服务器处理任务的状态。
* 接收服务器返回的结果文件或消息，并进行管理。

前端通过TCP/IP协议与后端服务器通信，采用自定义的应用层协议，确保数据传输的可靠性和效率。

**核心特性 (Core Features):**

* **现代化图形用户界面 (Modern GUI)**: 基于 Qt 6.9.0 构建，提供直观的文件浏览和任务管理界面。
* **本地文件系统集成 (Local Filesystem Integration)**: 使用 `QFileSystemModel` 实现高效的目录树和文件列表展示。
* **异步网络通信 (Asynchronous Network Communication)**:
  * 自定义的 `ClientSocket` 类，封装了TCP连接、消息收发逻辑。
  * 使用自定义协议（标签化消息头 + 负载）与后端通信。
  * 非阻塞操作和线程池 (`ThreadPool` from `network.hpp`) 用于处理网络事件和回调，确保UI流畅。
* **任务管理与追踪 (Task Management & Tracking)**:
  * `TaskManager` 类负责协调文件发送和接收任务的生命周期。
  * UI实时更新任务状态 (准备、进行中、等待服务器、完成、错误)。
* **模块化操作 (Modular Actions)**: 通过 `ActionTask` 基类及其派生类 (如 `SendFileTask`) 实现UI操作的解耦。
* **异步日志系统 (Asynchronous Logging System)**: 将详细的运行时信息、警告和错误异步记录到文件，便于调试和追踪。
* **跨平台潜力 (Cross-Platform Potential)**: Qt框架本身支持多平台，为项目的跨平台部署提供了基础。

## 📁 项目结构与核心组件

```
frontend/
├── graphic-interface/               # Qt 图形用户界面相关代码 (GUI Layer)
│   ├── base.ActionTask.hpp          # UI 操作任务抽象基类
│   ├── MainWindow.cpp               # 主窗口 runMainWindow 入口及全局 QApplication 管理
│   ├── class.MainWindow.cpp         # 主窗口 (QMainWindow) 逻辑实现
│   ├── graphic-interface.hpp        # GUI 层主要头文件，聚合UI类声明
│   ├── class.SendFileTask.cpp       # “发送文件”具体操作实现
│   └── class.TaskManager.cpp        # 前端异步任务管理器 (文件发送、保存)
├── network/                         # 网络通信层代码 (Networking Layer)
│   ├── class.ClientSocket.cpp       # TCP 客户端套接字核心实现
│   ├── class.ConnectionManager.cpp  # (ClientSocket内部) 连接管理辅助
│   ├── class.MessageHandler.cpp     # (ClientSocket内部) 接收消息分发处理
│   ├── class.Receiver.cpp           # (ClientSocket内部) 套接字数据接收逻辑
│   ├── class.Sender.cpp             # (ClientSocket内部) 套接字数据发送逻辑
│   └── network.hpp                  # 网络层主要头文件 (含ClientSocket, ThreadPool, Buffer等声明)
├── main.cpp                         # 应用程序主入口 (main function)
├── write-log.cpp                    # 异步日志记录功能实现 (Logging System)
├── cloud-compile-frontend.hpp       # 前端项目主要聚合头文件
└── frontend-defs.hpp                # 全局定义 (宏、常量、应用版本等)
```

### 1. `main.cpp` - 程序入口与全局初始化

* **作用**:
  * 作为整个前端应用程序的启动点。
  * 负责初始化核心服务，如网络连接和日志系统。
  * 创建并运行Qt应用程序主事件循环及主窗口。
* **`main()` 函数**:
    1. **`ClientSocket client(SERVER_IP, DEFAULT_PORT)`**: 创建 `ClientSocket` 实例，尝试与在 `frontend-defs.hpp` 中定义的默认服务器IP和端口建立连接。
    2. **消息处理器注册**:
        * `client.register_default_handler(...)`: 注册一个默认消息处理器，用于处理服务器发送的、没有特定标签匹配到的消息，通常记录警告日志。
        * `client.register_handler("Hello", ...)`: 注册针对 "Hello" 标签的消息处理器，通常用于记录服务器的欢迎信息。
        * `client.register_handler("error-information", ...)`: 注册针对 "error-information" 标签的消息处理器，用于记录服务器发送的错误信息。
    3. **`vector<string> args`**: 准备传递给 `runMainWindow` 的参数列表（当前为空）。
    4. **`return runMainWindow(client, args)`**: 调用 `graphic-interface/MainWindow.cpp` 中的 `runMainWindow` 函数，将 `ClientSocket` 实例和参数传递进去，启动GUI。
* **`global` 结构体 (RAII)**:
  * **作用**: 利用其构造和析构函数管理应用程序全局资源的生命周期，确保在程序启动时进行初始化，在程序退出时进行清理。这是一个典型的 RAII (Resource Acquisition Is Initialization) 应用。
  * **构造函数 `global::global()`**:
    * `make_sure_log_file()`: 调用 `write-log.cpp` 中的函数，确保日志系统（`Logger` 单例）被初始化。如果初始化失败，会打印错误到 `std::cerr` 并可能抛出异常。
    * **目录创建**: 使用 `std::filesystem` API 检查并创建必要的运行时目录：`LOG_DIRECTORY` ("cpl-log"), "bin", "src", `OUT_DIRECTORY` ("out")。如果创建失败，会记录错误日志。
  * **析构函数 `global::~global()`**:
    * `log_write_regular_information(...)`: 记录程序即将退出的信息。
    * `close_log_file()`: 调用 `write-log.cpp` 中的函数，确保日志系统 `Logger` 将所有缓冲的日志条目刷新到磁盘，并为后续的正常关闭做准备 (实际文件关闭在 `Logger` 析构中)。

### 2. `graphic-interface/` - 图形用户界面核心

#### 2.1. `MainWindow` 类 (`class.MainWindow.cpp`, `graphic-interface.hpp`)

* **作用**:
  * 作为应用程序的主窗口 (`QMainWindow`)，是用户与前端功能交互的主要界面。
  * 负责组织和管理所有UI组件，如文件浏览器、路径编辑器、任务列表等。
  * 处理用户输入事件，并将业务逻辑分派给其他组件（如 `TaskManager`）。
  * 响应来自其他组件（如 `TaskManager`, `ClientSocket`）的信号，更新UI状态。
* **核心实现与原理**:
  * **构造函数 `MainWindow::MainWindow(ClientSocket &sock, QWidget *parent)`**:
    * 初始化成员变量，包括对 `ClientSocket` 实例的引用 (`clientSocketInstance_`)。
    * 确定并创建用于保存接收文件的输出目录 (`outputDirectory_`)。它首先尝试 `QStandardPaths::AppLocalDataLocation` 下的路径，如果失败则使用文档目录下的备用路径，并确保路径存在。
    * 实例化 `TaskManager taskManager_`，将 `clientSocketInstance_` 和 `outputDirectory_` 传递给它。
    * `setupUi()`: 创建和布局所有UI控件。
    * `connectSignalsAndSlots()`: 连接各个UI控件的信号以及自定义信号到相应的槽函数。
    * `startNavigateToPath(QDir::homePath())`: 初始化文件浏览器视图，导航到用户的主目录。
  * **UI布局 (`setupUi()`)**:
    * `QFileSystemModel` (`fsTreeModel_`, `fsListModel_`): 两个实例分别用于驱动目录树视图 (`QTreeView *fsTree_`) 和文件/文件夹列表视图 (`QListView *fsList_`)。`fsTreeModel_`只显示目录，`fsListModel_`显示所有条目。
    * `QLineEdit *pathEdit_`: 地址栏，用于显示当前路径和接收用户输入的路径。
    * `QToolBar`: 创建主工具栏，并通过 `createToolbarActions()` 添加操作按钮（如“上一级”、“发送文件”）。
    * `QListWidget *taskListWidget_`: 用于显示网络任务（发送/接收）及其状态。
    * `QSplitter`: 用于灵活调整目录树、文件列表和任务区域的相对大小。
    * 设置窗口标题和初始大小。
  * **工具栏与动作 (`createToolbarActions()`, `executeActionTask()`)**:
    * 使用 `ActionTask` 模式（见 `base.ActionTask.hpp`）。`SendFileTask` 作为 `ActionTask` 的具体实现被创建。
    * `QAction` 对象 (如 `upAction_`, `sendAction_`) 被创建，并关联到相应的图标和文本。`sendAction_` 的启用状态由 `SendFileTask::canExecute()` 动态决定。
    * `toolbarActionTasks_` (一个 `std::map<QAction *, unique_ptr<ActionTask>>`) 存储 `QAction` 和其对应的 `ActionTask` 实例。
    * 当工具栏按钮被触发时，对应的 `QAction` 的 `triggered` 信号会间接调用 `executeActionTask()`（在新版中直接连接lambda到 `ActionTask::execute`），进而执行 `ActionTask` 的 `execute()` 方法。
  * **信号槽连接 (`connectSignalsAndSlots()`)**:
    * 连接 `QTreeView`, `QListView`, `QLineEdit` 的用户交互信号（如 `clicked`, `doubleClicked`, `returnPressed`）到 `MainWindow` 的槽函数（如 `onTreeViewClicked`, `onListViewDoubleClicked`, `onPathLineEditReturnPressed`）。
    * 连接 `TaskManager` 的信号（如 `sendFileInitiationCompleted`, `receivedFileSaveCompleted`）到 `MainWindow` 的槽函数，以在后台任务完成时更新UI。
    * 连接 `QFutureWatcher` (如 `navigationWatcher_`, `openFileWatcher_`) 的 `finished` 信号来处理异步导航和文件打开操作的结果。
    * 通过 `clientSocketInstance_.register_handler("compile-execute", ...)` 注册一个lambda表达式作为网络消息处理器。当收到来自服务器的 "compile-execute" 标签的消息时，此lambda会被调用。它解析payload（包含原始文件名和可选的文件数据），然后通过 `QMetaObject::invokeMethod` 以队列连接方式调用 `handleServerFileResponse` 在主线程中处理。
  * **文件系统导航 (`onTreeViewClicked`, `onListViewDoubleClicked`, `onPathLineEditReturnPressed`, `onGoUpActionTriggered`, `startNavigateToPath`, `performNavigationTask`, `handleNavigationFinished`)**:
    * 用户操作会触发对 `startNavigateToPath(path)` 的调用。
    * `startNavigateToPath` 使用 `QtConcurrent::run` 启动一个后台任务 `performNavigationTask(path)` 来验证路径的有效性（是否存在、是否是目录、是否可读）。
    * `navigationWatcher_` (`QFutureWatcher<pair<bool, QString>>`) 监视此后台任务。任务完成后，`handleNavigationFinished` 被调用。
    * `handleNavigationFinished` 根据验证结果更新 `fsListModel_` 的根路径、`fsTree_` 的当前项、`pathEdit_` 的文本，并处理可能的错误提示。
  * **文件发送处理 (`onSendFileInitiationCompleted`)**:
    * 当 `TaskManager` 发出 `sendFileInitiationCompleted` 信号时此槽被调用。
    * 根据发送启动是否成功，更新 `taskListWidget_` 中对应任务项的UI状态（例如，更新为“等待服务器响应”或“发送失败”）。如果失败，则从 `activeSendTaskItems_` 中移除该任务。
  * **服务器响应处理 (`handleServerFileResponse`, `parseEchoPayload`)**:
    * 由 `ClientSocket` 的消息处理器在主线程中调用。
    * `parseEchoPayload`: 一个静态辅助函数，用于从服务器返回的原始字节串中解析出原始文件名和文件数据。协议假定文件名后紧跟 `\0`，然后是文件数据。
    * `handleServerFileResponse`:
      * 根据 `originalFileNameFromServer` 查找对应的正在发送的任务项 (`activeSendTaskItems_`，这是一个 `std::map<QString, QListWidgetItem *>`，通过 `QMutex activeSendTasksMutex_` 保护并发访问)。
      * 如果找到任务项，根据服务器处理是否成功 (`serverProcessingSuccess`) 和返回的消息 (`serverMessage`) 更新其UI状态。
      * 如果服务器返回了文件数据 (`!fileData.empty()`) 且原始payload解析成功，则会再次调用 `TaskManager::workerSaveReceivedFile` (通过 `QtConcurrent::run` 包装并由 `QFutureWatcher` 监视) 将这个“回显”的文件数据保存到本地。保存完成后，会更新原任务项的工具提示，并可能更新其 `FilePathRole` 数据。
      * 如果未找到匹配的发送任务项但服务器成功返回了数据，也会尝试静默保存该文件。
  * **接收文件保存完成处理 (`onReceivedFileSaveCompleted`)**:
    * 当 `TaskManager` 保存一个**服务器主动推送的、或作为先前发送操作的回显**的文件成功或失败后，会发出 `receivedFileSaveCompleted` 信号，此槽被调用。
    * 它会在 `taskListWidget_` 中创建一个新的任务项（如果这是一个新的接收任务）或更新现有项，显示保存成功或失败的状态。
  * **任务项UI更新 (`updateUITaskItem`, `updateMainWindowUITaskItem`)**:
    * `updateUITaskItem` 是核心的UI更新函数，根据任务状态 (`UITaskStatus`) 设置任务项的文本、图标、颜色、工具提示以及自定义角色数据 (如 `UITaskStatusRole`, `FilePathRole`, `ErrorMessageRole`)。
    * `updateMainWindowUITaskItem` 是一个简单的包装，直接调用 `updateUITaskItem`。
  * **状态管理**:
    * `isAFileSelected()`, `getSelectedFilePath()`: 用于检查当前列表视图中是否有文件被选中，并获取其路径。
    * `activeSendTaskItems_` 和 `activeSendTasksMutex_`: 管理当前正在发送并等待服务器响应的任务，确保文件名唯一且线程安全。
  * **析构 (`~MainWindow()`)**: 取消并等待任何正在运行的 `QFutureWatcher` 任务 (导航、打开文件)。

#### 2.2. `TaskManager` 类 (`class.TaskManager.cpp`, `graphic-interface.hpp`)

* **作用**:
  * 作为 `MainWindow` 的一个辅助类，负责封装和管理与文件传输相关的后台异步操作。
  * 解耦了 `MainWindow` 与直接的、可能阻塞的IO操作（如文件读写、网络调用准备）。
* **核心实现与原理**:
  * **构造函数 `TaskManager::TaskManager(ClientSocket &sock, const QString &outDir, QObject *parent)`**:
    * 保存对 `ClientSocket` 实例的引用 (`socket_`) 和输出目录 (`outputDir_`)。
  * **`initiateSendFile(const QString &absoluteFilePath)`**:
    * 此方法被调用以开始一个文件发送过程。
    * 使用 `QtConcurrent::run(&TaskManager::workerSendFileInitiation, &socket_, absoluteFilePath)` 将文件发送的准备和初步网络调用放到一个单独的工作线程中。
    * 创建一个 `QFutureWatcher<tuple<QString, bool, QString>>` 来监视这个异步操作。
    * 当工作线程完成时，`QFutureWatcher` 的 `finished` 信号被触发，其连接的lambda会获取结果（原始文件路径、操作是否成功、错误原因），然后 `emit sendFileInitiationCompleted(...)` 信号，通知 `MainWindow`。
  * **`workerSendFileInitiation(ClientSocket *socket, QString filePath)` (static)**:
    * 在工作线程中执行。
    * 检查 `socket` 是否连接，如果未连接则尝试 `socket->connect()`。
    * 如果连接成功或已连接，调用 `socket->send_file("compile-execute", filePath.toStdString())`。这里 "compile-execute" 是发送给服务器的协议标签。
    * 捕获 `send_file` 可能出现的异常或返回的错误。
    * 返回一个包含文件路径、成功状态和错误消息的 `std::tuple`。
  * **`saveReceivedFile(const QString &originalFileName, const std::vector<char> &fileData)`**:
    * 当 `MainWindow` 从服务器接收到文件数据（例如通过 `handleServerFileResponse`）并需要保存时，会调用此方法。
    * 与 `initiateSendFile` 类似，它使用 `QtConcurrent::run(&TaskManager::workerSaveReceivedFile, originalFileName, fileData, outputDir_)` 将文件保存操作放到工作线程。
    * 同样使用 `QFutureWatcher<tuple<QString, QString, bool, QString>>` 监视，并在完成后 `emit receivedFileSaveCompleted(...)` 信号。
  * **`workerSaveReceivedFile(QString originalFileName, const std::vector<char> &fileDataVec, QString outputDir)` (static)**:
    * 在工作线程中执行。
    * 生成一个基于当前时间戳的文件名 (如 `[timestamp].txt`)，以避免与原始文件名冲突或覆盖。
    * 检查并创建 `outputDir` (如果不存在)。
    * 使用 `QFile` 将 `fileDataVec` 中的数据写入到生成的保存路径。
    * 处理文件打开、写入、关闭过程中的各种错误，例如磁盘空间不足、权限问题等。
    * 如果写入失败但文件已创建，会尝试删除该不完整文件。
    * 返回一个包含原始文件名、实际保存路径、成功状态和错误消息的 `std::tuple`。
  * **信号机制**: `sendFileInitiationCompleted` 和 `receivedFileSaveCompleted` 是 `TaskManager` 与 `MainWindow` 沟通的主要方式，实现了异步操作结果的回传。
  * **线程安全**: `TaskManager` 本身的方法（如 `initiateSendFile`）是在主线程中调用的，它们通过 `QtConcurrent` 将工作分派到其他线程。静态的 `worker` 函数在工作线程中执行，它们访问的 `ClientSocket*` 或数据副本需要考虑其生命周期和并发访问（`ClientSocket` 内部有自己的线程安全机制）。传递给 `workerSaveReceivedFile` 的 `fileDataVec` 是值传递（拷贝），保证了数据的线程安全。

#### 2.3. `SendFileTask` 类 (`class.SendFileTask.cpp`, `base.ActionTask.hpp`)

* **作用**:
  * 作为 `ActionTask` 接口的具体实现，封装了用户通过UI触发“发送文件”这一动作的全部逻辑。
  * 使得“发送文件”功能可以被模块化地添加到 `MainWindow` 的工具栏或其他UI元素上。
* **核心实现与原理 (继承自 `ActionTask`)**:
  * **构造函数 `SendFileTask::SendFileTask(QObject *parent)`**: 简单的基类构造函数调用。
  * **`actionText() const override`**: 返回在UI上显示的动作名称，例如 "发送文件"。
  * **`actionIcon() const override`**: 返回用于UI按钮的图标，例如使用 `QIcon::fromTheme` 或从资源文件加载。
  * **`canExecute(MainWindow *mainWindowContext) const override`**:
    * 决定此动作当前是否可用。
    * 在 `SendFileTask` 中，它会调用 `mainWindowContext->isAFileSelected()` 来检查主窗口的文件列表中是否已选中一个文件。只有选中了文件，发送操作才能执行。
  * **`execute(MainWindow *mainWindowContext) override`**:
    * 当用户触发此动作时被调用。
    * 首先检查 `mainWindowContext` 是否为空，并再次调用 `canExecute()` 进行确认。如果不能执行，可能会显示提示信息 (如 `QMessageBox::information`)。
    * 调用 `mainWindowContext->getSelectedFilePath()` 获取选中文件的完整路径。
    * **防止重复任务**: 使用 `mainWindowContext->getActiveSendTaskItemsMutex()` 加锁，检查 `mainWindowContext->getActiveSendTaskItems()` 中是否已存在同名文件的发送任务。如果存在，则提示用户任务已在进行中并返回。
    * **创建任务列表项**: 创建一个新的 `QListWidgetItem`。设置其自定义角色数据：`OriginalFileNameRole` (文件名) 和 `FilePathRole` (完整路径)。
    * 将新创建的 `QListWidgetItem` 添加到 `mainWindowContext->getTaskListWidget()` 中。
    * 调用 `mainWindowContext->updateMainWindowUITaskItem()` 初始化该任务项的UI状态为 `UITaskStatus::Preparing`。
    * 再次加锁 `getActiveSendTaskItemsMutex()`，将此任务项存入 `mainWindowContext->getActiveSendTaskItems()`，以文件名作为键。
    * **核心调用**: 调用 `mainWindowContext->getTaskManager().initiateSendFile(filePath)`，将实际的文件发送准备工作委托给 `TaskManager`。

### 3. `network/` - 网络通信核心

#### 3.1. `ClientSocket` 类 (`class.ClientSocket.cpp`, `network.hpp`)

* **内部组件概览**: `ClientSocket` 的强大功能离不开其内部精心设计的辅助类。这些类各司其职，共同构成了健壮的网络通信基础：
  * `ConnectionManager`: 负责物理连接的建立与socket管理。
  * `Sender`: 管理消息的异步发送队列和实际的socket写操作。
  * `Receiver`: 负责从socket异步读取数据并进行初步缓冲。
  * `MessageHandler`: 解析接收到的数据帧，并将消息分派给注册的处理器。
    下面将对这些内部组件进行更详细的阐述。

#### 3.2. `ConnectionManager` 类 (`class.ConnectionManager.cpp`, `network.hpp` 中声明为 `ClientSocket` 的私有内部类)

* **作用**:
  * 作为 `ClientSocket` 的一个私有辅助类，专门负责处理TCP连接的建立细节和底层socket的关闭。
  * 它将平台相关的socket操作（如创建、连接、设置非阻塞、关闭）封装起来，使 `ClientSocket` 的主逻辑更清晰。
* **核心实现与原理**:
  * **持有者引用**: 构造时接收一个 `ClientSocket& owner_` 的引用，以便访问 `owner_` 的配置（如服务器IP和端口）和日志函数。
  * **`try_connect(void)`**:
        1. **创建套接字**: 调用 `::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)` 创建一个TCP套接字。`SOCK_CLOEXEC` 确保在执行 `exec` 系列函数时此描述符被关闭，是一个良好的安全实践。
        2. **服务器地址设置**: 填充 `sockaddr_in server_addr` 结构体，使用 `owner_.server_ip_` 和 `owner_.server_port_`。通过 `inet_pton()` 将点分十进制的IP地址转换为网络字节序的二进制形式。
        3. **建立连接**: 调用 `::connect(temp_sockfd, ...)` 尝试与服务器建立连接。
        4. **设置为非阻塞**: 如果连接成功，通过 `fcntl(temp_sockfd, F_GETFL, 0)` 获取当前套接字标志，然后通过 `fcntl(temp_sockfd, F_SETFL, flags | O_NONBLOCK)` 添加 `O_NONBLOCK` 标志，使后续的socket操作（如 `send`, `recv`）变为非阻塞。
        5. **错误处理**: 在上述任何一步失败时（例如 `socket() < 0`, `inet_pton() <= 0`, `connect() < 0`, `fcntl()` 失败），都会记录错误日志，关闭已创建的套接字（如果存在），并返回 `-1` 表示连接失败。
        6. 成功时返回创建并配置好的套接字文件描述符。
  * **`close_socket(int &sockfd_ref)`**:
    * 接受一个文件描述符的引用。
    * 如果 `sockfd_ref` 不是 `-1`（表示是一个有效的、打开的套接字）：
      * 调用 `shutdown(sockfd_ref, SHUT_RDWR)` 来优雅地关闭双向的连接，这会尝试发送FIN包。
      * 调用 `close(sockfd_ref)` 彻底关闭文件描述符并释放相关资源。
      * 将 `sockfd_ref` 重置为 `-1`，表示套接字已关闭。

#### 3.3. `Sender` 类 (`class.Sender.cpp`, `network.hpp` 中声明为 `ClientSocket` 的私有内部类)

* **作用**:
  * 作为 `ClientSocket` 的发送逻辑核心，负责管理一个待发送消息的队列，并在一个独立的发送线程中异步地将这些消息写入TCP套接字。
  * 实现了发送操作与主调用线程的解耦，防止 `send()` 操作阻塞上层逻辑。
* **核心实现与原理**:
  * **持有者引用**: 构造时接收 `ClientSocket& owner_`。
  * **发送队列 (`send_queue_`)**: 一个 `std::queue<tuple<unique_ptr<char[]>, size_t>>`。每个元素是一个元组，包含一个指向消息数据缓冲区的 `unique_ptr<char[]>` (负责内存管理) 和消息的实际长度。
  * **同步机制**:
    * `send_mutex_ (std::mutex)`: 用于保护对 `send_queue_` 的并发访问。
    * `send_cv_ (std::condition_variable)`: 用于在队列为空时阻塞发送线程，并在新消息入队或请求停止时唤醒它。
  * **`enqueue_message(unique_ptr<char[]> message, size_t msglen)`**:
    * 此方法由 `ClientSocket` 的 `send_message` 等接口调用。
    * 使用 `std::lock_guard<std::mutex> lock(send_mutex_)` 获取互斥锁。
    * 将 `std::move(message)` 和 `msglen` 放入 `send_queue_`。
    * 释放锁。
    * 调用 `send_cv_.notify_one()` 唤醒可能正在等待的 `send_loop()` 线程。
  * **`clear_queue()`**: 在断开连接时调用，清空发送队列中所有未发送的消息。
  * **`send_loop()` (运行在 `owner_.send_thread_` 中)**:
        1. 进入一个循环，该循环会持续运行直到 `owner_.stop_requested_` (一个 `std::atomic<bool>`) 为 `true`。
        2. **等待任务**: 使用 `std::unique_lock<std::mutex> lock(send_mutex_)` 获取锁，然后调用 `send_cv_.wait(lock, ...)`。等待条件是 `owner_.stop_requested_` 为 `true` **或者** `send_queue_` 非空。
        3. **检查停止请求**: 被唤醒后，首先检查 `owner_.stop_requested_`。如果为 `true`，则跳出循环，线程结束。
        4. **获取消息**: 从 `send_queue_` 中取出队首消息 (`send_queue_.front()`, `send_queue_.pop()`)。
        5. 释放锁。
        6. **发送数据**: 调用 `send_all_internal(msg_ptr, msg_len)` 将取出的消息数据发送出去。
        7. **错误处理**: 如果 `send_all_internal` 返回 `false`（表示发送失败，通常意味着连接已断开），则记录错误，通过 `owner_.trigger_error_callback_internal()` 通知上层，并调用 `owner_.request_disconnect_async_internal()` 请求异步断开连接（这会设置 `stop_requested_` 并最终停止此循环），然后 `break` 退出循环。
  * **`send_all_internal(const char *data, size_t len)` (private)**:
    * 负责将指定长度 (`len`) 的数据 (`data`) 完全发送到 `owner_.sockfd_`。
    * 使用一个 `while` 循环，只要 `total_sent < len` 且 `!owner_.stop_requested_` 就持续发送。
    * **检查socket有效性**: 每次循环前检查 `current_sockfd` 是否为-1。
    * **执行发送**: 调用 `::send(current_sockfd, data + total_sent, len - total_sent, MSG_NOSIGNAL)`。`MSG_NOSIGNAL` 防止在对方连接重置时产生 `SIGPIPE` 信号。
    * **处理 `send()` 返回值**:
      * `sent > 0`: 成功发送一部分或全部数据，更新 `total_sent`。
      * `sent == 0`: 非典型情况，通常表示对方关闭连接（但 `recv` 更常用于检测这个），这里视为发送失败。
      * `sent < 0`: 发生错误。
        * **`EAGAIN` 或 `EWOULDBLOCK`**: 表示socket发送缓冲区已满，当前不可写。此时，使用 `poll()` (带超时) 等待 `current_sockfd` 变为可写 (`POLLOUT`)。如果 `poll` 超时或返回错误（非 `EINTR`），则视为发送失败。
        * **`EINTR`**: 如果被信号中断且未请求停止，则继续尝试发送。
        * **其他错误**: 记录错误，视为发送失败。
    * 如果循环因 `owner_.stop_requested_` 而退出，或发生不可恢复的错误，函数返回 `false`。完全发送成功则返回 `true`。
  * **`notify_sender()`**: `ClientSocket` 在请求断开时调用此方法，它简单地 `send_cv_.notify_one()` 来确保即使队列为空，`send_loop` 也能被唤醒并检查 `stop_requested_` 标志。

#### 3.4. `Receiver` 类 (`class.Receiver.cpp`, `network.hpp` 中声明为 `ClientSocket` 的私有内部类)

* **作用**:
  * 作为 `ClientSocket` 的接收逻辑核心，在一个独立的接收线程中运行，负责从TCP套接字异步读取数据，并将数据存入一个内部的 `Buffer` 对象。
  * 当读取到数据后，它会调用 `MessageHandler` 来处理这些数据。
* **核心实现与原理**:
  * **持有者引用**: 构造时接收 `ClientSocket& owner_`。
  * **接收缓冲区 (`recv_buffer_`)**: 一个 `ClientSocket::Buffer` 实例，用于存储从socket读取的原始字节流。
  * **`clear_buffer()`**: 在断开连接时调用，清空 `recv_buffer_` 中所有未处理的数据。
  * **`recv_loop()` (运行在 `owner_.recv_thread_` 中)**:
        1. 进入一个循环，持续运行直到 `owner_.stop_requested_` 为 `true`。
        2. **获取当前socket**: `current_sockfd = owner_.sockfd_.load(std::memory_order_relaxed)`。如果为-1，则表示连接已关闭，循环终止。
        3. **使用 `poll()` 进行I/O多路复用**:
            *创建 `struct pollfd pfd`，设置 `pfd.fd = current_sockfd`，关注的事件为 `POLLIN | POLLPRI` (普通或优先数据可读)。
            * 调用 `poll(&pfd, 1, poll_timeout_ms)` (超时时间如200ms)。这会阻塞线程，直到socket上有事件发生或超时。
        4. **检查停止请求**: `poll` 返回后，再次检查 `owner_.stop_requested_`。
        5. **处理 `poll()` 返回值**:
            *`poll_ret < 0`: `poll` 失败。如果 `errno == EINTR` (被信号中断)，则继续循环。否则，记录错误，通过 `owner_.trigger_error_callback_internal()` 和 `owner_.request_disconnect_async_internal()` 处理，并 `break`。
            * `poll_ret == 0`: 超时，表示在 `poll_timeout_ms` 期间没有事件发生。继续下一次循环。
            *`poll_ret > 0`: 有事件发生。
                * **错误事件检查**: 检查 `pfd.revents` 是否包含 `POLLERR | POLLHUP | POLLNVAL` (socket错误、挂断、无效请求)。如果是，记录详细错误（可能通过 `getsockopt(SO_ERROR)` 获取具体错误码），并同样触发错误回调和断开请求，然后 `break`。
                ***可读事件处理**: 如果 `pfd.revents` 包含 `POLLIN | POLLPRI`，表示socket可读。
                    * 调用 `recv_buffer_.read_fd(current_sockfd, &saved_errno)` 从socket读取数据到 `recv_buffer_`。
                    ***处理 `read_fd()` 返回值 `n`**:
                        * `n > 0`: 成功读取 `n` 字节数据。调用 `owner_.message_handler_->process_received_data(recv_buffer_)` 来尝试解析和处理刚接收到的数据。如果 `message_handler_` 为空，则记录错误。
                        *`n == 0`: 表示对端关闭了连接 (EOF - End Of File)。记录此信息，调用 `owner_.request_disconnect_async_internal("Peer closed connection")`，并 `break`。
                        * `n < 0`: 读取失败。如果 `saved_errno` 是 `EAGAIN`、`EWOULDBLOCK` 或 `EINTR`，则忽略并继续循环（尽管在阻塞 `poll` 之后 `EAGAIN`/`EWOULDBLOCK` 理论上不应立即发生，除非socket状态在 `poll` 和 `read` 之间改变了）。其他错误则记录，触发错误回调和断开请求，并 `break`。

#### 3.5. `MessageHandler` 类 (`class.MessageHandler.cpp`, `network.hpp` 中声明为 `ClientSocket` 的私有内部类)

* **作用**:
  * 作为 `ClientSocket` 的消息分派中心，负责从 `Receiver` 提供的 `Buffer` 中解析出符合自定义应用层协议的消息帧，并根据消息的“标签”(tag) 将消息负载(payload)分派给上层（如 `ClientSocket` 的使用者）注册的相应处理器函数。
* **核心实现与原理**:
  * **持有者引用**: 构造时接收 `ClientSocket& owner_`。
  * **处理器存储**:
    * `handlers_ (std::unordered_map<string, Handler>)`: 存储特定标签字符串到其对应处理函数 (`Handler = function<void(const string &payload)>`) 的映射。
    * `default_handler_ (Handler)`: 如果没有找到特定标签的处理器，则调用此默认处理器。
    * `handler_rw_mutex_ (std::shared_mutex)`: 用于保护对 `handlers_` 和 `default_handler_` 的并发读写访问（读时共享，写时独占），确保注册和查找操作的线程安全。
  * **`register_handler(const string &tag, Handler handler)`**:
    * 获取 `std::unique_lock` 以独占访问 `handler_rw_mutex_`。
    * 将 `tag` 和 `handler` 存入 `handlers_`。如果 `handler` 为空，则记录警告。
  * **`register_default_handler(Handler handler)`**:
    * 类似地，获取唯一锁并设置 `default_handler_`。
  * **`process_received_data(Buffer &recv_buffer)`**:
    * 此方法被 `Receiver::recv_loop()` 在接收到新数据后调用。
    * **循环解析**: 进入一个 `while(true)` 循环，尝试从 `recv_buffer` 中解析尽可能多的完整消息帧。
    * **协议帧格式**: `[1-byte tag_len][tag_string (tag_len bytes)][4-byte payload_len_network_order][payload_data (payload_len bytes)]`
          1. **读取 `tag_len`**: 检查 `recv_buffer.readable_bytes()` 是否至少为1。如果是，通过 `*recv_buffer.peek()` 获取 `tag_len` (一个 `uint8_t`)。
          2. **检查头部完整性**: 计算完整的头部长度 `header_len = 1 + tag_len + sizeof(uint32_t)`。检查 `recv_buffer.readable_bytes()` 是否小于 `header_len`。如果不足，表示当前数据不足以解析完整头部，`break` 退出循环，等待更多数据。
          3. **读取 `payload_len`**: 从 `recv_buffer.peek() + 1 + tag_len` 位置拷贝4字节到 `uint32_t payload_len_net`，然后通过 `ntohl(payload_len_net)` 转换为主机字节序得到 `payload_len`。
          4. **Payload大小检查**: 检查 `payload_len` 是否超过 `Buffer::kMaxFrameSize`。如果超过，这是一个严重的协议错误（可能导致分配过多内存），记录错误，调用 `owner_.trigger_error_callback_internal()` 和 `owner_.request_disconnect_async_internal()`，清空 `recv_buffer` 并 `return`（终止进一步处理）。
          5. **检查消息完整性**: 计算总消息长度 `total_message_len = header_len + payload_len`。检查 `recv_buffer.readable_bytes()` 是否小于 `total_message_len`。如果不足，`break` 退出循环，等待更多数据。
    * **提取消息并分派**: 如果上述检查都通过，表示 `recv_buffer` 中至少包含一个完整的消息帧。
          1. **提取 `tag`**: `std::string tag(recv_buffer.peek() + 1, tag_len)`。
          2. **消耗头部**: `recv_buffer.retrieve(header_len)`。
          3. **提取 `payload`**: `std::string payload = recv_buffer.retrieve_as_string(payload_len)`。
          4. **查找处理器**:
              *获取 `std::shared_lock` 以共享访问 `handler_rw_mutex_`。
              * 在 `handlers_` 中查找 `tag`。如果找到，`handler_to_call` 指向对应的 `Handler`。
              *如果未找到，但 `default_handler_` 已设置，则 `handler_to_call` 指向 `default_handler_`。
              * 释放共享锁。
            5. **执行处理器**:
              *如果找到了有效的 `handler_to_call`：
                  * 通过 `owner_.thread_pool_.enqueue(0, [h = std::move(handler_to_call), p = std::move(payload), tag_copy = tag]() mutable { ... })` 将处理器的执行（包装在一个lambda中，捕获处理器、payload副本和tag副本）提交到 `ClientSocket` 的线程池中异步执行。这避免了消息处理逻辑阻塞接收线程。
                  *Lambda内部包含 `try-catch`块，以捕获处理器执行时可能抛出的异常，并记录错误。
              * 如果没有找到处理器（特定或默认的），则记录一条警告日志，指出该消息被丢弃。
    * 循环继续，尝试从 `recv_buffer` 的剩余数据中解析下一个消息帧。

#### 3.6. `ThreadPool` 类 (定义于 `network.hpp`)

* **作用**:
  * 提供一个全局单例的、基于优先级的线程池，用于执行应用程序中的异步任务，特别是 `ClientSocket` 的消息处理回调和错误/连接状态回调。
* **核心实现与原理**:
  * **单例模式**: `ThreadPool::instance()` 获取全局唯一的实例。
  * **构造函数 `ThreadPool(size_t max_threads_param)`**:
    * 初始化最大线程数 (`max_threads_`)，默认为 `std::thread::hardware_concurrency()`。
    * 预留工作线程存储空间 (`workers_`)。
  * **任务队列 (`tasks_`)**: 一个 `std::priority_queue<TaskWrapper, vector<TaskWrapper>, Compare>`。
    * `TaskWrapper`: 包含任务的优先级 (`priority`)、序列号 (`seq`，用于同优先级时先进先出) 和实际要执行的函数对象 (`Task = function<void()>`)。
    * `Compare`: 自定义比较器，优先处理高优先级任务；同优先级时，序列号小的任务优先。
  * **任务入队 (`enqueue`, `enqueue_internal`)**:
    * `enqueue(priority, F&& f, Args&&... args)`: 模板方法，接受优先级、函数及参数。
    * 使用 `std::packaged_task` 包装待执行函数，以便获取其 `std::future`。
    * 将 `packaged_task` 包装成一个lambda（`TaskWrapper::func`），放入优先队列 `tasks_`。
    * 如果当前空闲线程为0且工作线程数未达到上限，则创建新的工作线程 (`workers_.emplace_back(&ThreadPool::worker_thread, this)`)。
    * 通过条件变量 `cv_` 通知一个等待的工作线程。
    * 返回 `std::future<R>`，调用者可以用它来等待任务完成并获取结果。
  * **工作者线程 (`worker_thread`)**:
    * 每个工作线程运行一个循环。
    * 在循环中，加锁并使用条件变量 `cv_` 等待，直到任务队列 `tasks_` 非空或线程池被要求停止 (`stop_`)。
    * 如果线程池停止且任务队列为空，则线程退出。
    * 从任务队列中取出优先级最高的任务 (`tasks_.top()`, `tasks_.pop()`)。
    * 解锁，然后执行任务 (`task_to_run.func()`)。捕获并记录任务执行过程中可能发生的异常。
  * **线程池关闭 (`~ThreadPool()`)**:
    * 设置 `stop_` 标志为 `true`。
    * `cv_.notify_all()` 唤醒所有等待的线程。
    * `join()` 所有工作线程，等待它们安全退出。
  * **状态变量**:
    * `mtx_` (mutex), `cv_` (condition_variable): 用于同步对任务队列的访问和线程等待/唤醒。
    * `stop_` (atomic<bool>): 标志线程池是否应停止。
    * `seq_` (atomic<size_t>): 用于生成任务序列号。
    * `idle_threads_` (atomic<size_t>): 当前空闲的线程数。

#### 3.7. `Buffer` 类 (定义于 `network.hpp`)

* **作用**:
  * 为 `ClientSocket` 提供一个灵活高效的网络接收缓冲区。
  * 管理原始字节数据，提供方便的读写接口，并能自动增长。
* **核心实现与原理**:
  * **内存布局**: 内部使用 `std::vector<char> buffer_`。设计为 `[prependable_bytes (kPrependSize)] + [readable_bytes] + [writable_bytes]`。
    * `kPrependSize` (默认8字节): 缓冲区头部预留空间，方便在数据前添加协议头等，而无需移动现有数据（尽管在客户端的 `ClientSocket` 中，主要用于接收，`prepend` 使用较少）。
    * `read_index_`: 指向可读数据的起始位置。
    * `write_index_`: 指向可写区域的起始位置（也是已写入数据的末尾）。
  * **核心方法**:
    * `readable_bytes()`: 返回 `write_index_ - read_index_`。
    * `writable_bytes()`: 返回 `buffer_.size() - write_index_`。
    * `peek()`: 返回指向可读数据开头的 `const char*`。
    * `retrieve(len)`: 将 `read_index_` 前移 `len`，表示数据已被消耗。如果消耗所有可读数据，则调用 `retrieve_all()` 将读写索引重置。
    * `append(data, len)`: 将数据追加到写指针位置，并更新 `write_index_`。会调用 `ensure_writable_bytes` 来确保空间。
    * `ensure_writable_bytes(len)`: 如果可写空间不足 `len`：
      * 如果 `writable_bytes() + prependable_bytes()` (总未使用空间，头部可回收空间+尾部可写空间) 小于 `len + kPrependSize`，则直接 `buffer_.resize(write_index_ + len)` 来扩展 `vector` 的容量。
      * 否则，表示可以通过将当前可读数据 (`readable_bytes()`) 移动到缓冲区的开头 (从 `kPrependSize` 开始) 来腾出足够的连续可写空间。使用 `memmove` 完成数据迁移，然后更新 `read_index_` 和 `write_index_`。
    * `read_fd(fd, saved_errno)`: **关键的读取函数**。
      * 使用 `readv` (分散读) 系统调用来从文件描述符 `fd` 读取数据，这是一个性能优化点。
      * 准备两个 `iovec`：第一个指向 `Buffer` 内部当前的可写空间 (`begin_write()`, `writable_bytes()`)，第二个指向一个栈上的临时缓冲区 `extrabuf` (64KB)。
      * 如果读取的数据量 `n` 小于等于内部可写空间，则直接更新 `write_index_` (`has_written(n)`)。
      * 如果 `n` 大于内部可写空间（意味着 `extrabuf` 也被使用了），则先填满内部可写空间，然后调用 `append(extrabuf, ...)` 将 `extrabuf` 中的剩余数据追加到 `Buffer`（此时 `append` 内部的 `ensure_writable_bytes` 可能会触发 `vector` 扩容）。
      * 返回实际读取的字节数，或在出错时返回-1并设置 `*saved_errno`。读取到EOF时返回0。

### 4. `write-log.cpp` - 异步日志系统

* **`Logger` 类 (单例)**:
  * **作用**: 提供一个全局的、线程安全的、异步的日志记录服务。应用程序通过高级API函数（如 `log_write_regular_information`）记录日志，实际的磁盘写入操作由一个后台线程完成。
  * **核心实现与原理**:
    * **单例模式 (`getInstance()`)**: 确保整个应用程序中只有一个 `Logger` 实例。构造函数是私有的。首次调用 `getInstance()` 时会创建实例并启动后台日志写入线程。
    * **初始化 (`Logger::Logger()`)**:
      * 根据当前时间生成日志文件名 (如 `cpl-front-%timestamp%.log`) 并尝试在 `LOG_DIRECTORY` 下打开 `std::ofstream log_file_`。
      * 如果文件打开失败，会输出错误到 `std::cerr`，设置 `is_initialized_` 为 `false`，并可能抛出 `std::runtime_error`。
      * 如果文件打开成功，则启动一个后台线程 `writer_thread_`，该线程运行 `Logger::worker()` 方法。
      * 设置 `is_initialized_` (atomic) 为 `true`。
    * **日志入队 (`enqueueLog(string &&log_entry)`)**:
      * 此方法供外部API（如 `log_write_regular_information`）调用。
      * 如果日志系统未初始化 (`!is_initialized_`) 或已请求关闭 (`shutdown_requested_`)，则直接返回。
      * 使用 `std::lock_guard<std::mutex> lock(queue_mutex_)` 保护对日志队列 `log_queue_` (一个 `std::queue<string>`) 的并发访问。
      * 将格式化后的日志条目 `std::move` 到队列中。
      * 通过 `std::condition_variable cv_` 的 `notify_one()` 唤醒可能正在等待的 `writer_thread_`。
    * **后台写入线程 (`Logger::worker()`)**:
      * 在一个无限循环中运行，直到 `shutdown_requested_` 为 `true` 且日志队列为空。
      * 使用 `std::unique_lock<std::mutex> lock(queue_mutex_)` 和 `cv_.wait(lock, ...)` 使线程阻塞等待，直到 `log_queue_` 非空或收到关闭请求。
      * 当被唤醒且条件满足时：
        * 将主日志队列 `log_queue_` 的内容通过 `swap` 高效地转移到一个局部队列 `local_queue` 中。这样做是为了最小化持有锁的时间，让其他线程可以更快地入队新日志。
        * 解锁 `queue_mutex_`。
        * 遍历 `local_queue`，将每条日志写入 `log_file_`。如果文件流状态不好 (`!log_file_.good()`)，则输出错误到 `std::cerr`。
        * 在处理完一批（`local_queue`中所有）日志后，调用 `log_file_.flush()` 将缓冲区数据刷到磁盘。
    * **关闭与资源清理 (`Logger::~Logger()`)**:
      * 设置 `shutdown_requested_` (atomic) 为 `true`。
      * 调用 `cv_.notify_all()` 确保 `writer_thread_` 被唤醒以检查关闭标志。
      * 如果 `writer_thread_` 是可加入的 (`joinable()`)，则调用 `writer_thread_.join()` 等待其安全退出。
      * 在 `writer_thread_` 退出后，关闭 `log_file_` (如果已打开)。
    * **日志格式化 (`formatLogMessage(level, information)`)**:
      * 一个静态辅助函数，使用 `std::chrono` 获取精确到微秒的当前时间，并结合 `std::put_time` 将其格式化为 `YYYY-MM-DD HH:MM:SS.micros` 的形式，然后附加日志级别和日志内容。
    * **公共API函数 (`log_write_xxx_information`)**:
      * 这些函数（如 `log_write_regular_information`, `log_write_error_information`）是外部模块记录日志的接口。
      * 它们内部调用 `formatLogMessage` 格式化日志，然后调用 `Logger::getInstance().enqueueLog()` 将日志条目放入队列。
      * 包含了对 `Logger::getInstance()` 可能抛出异常（如果初始化失败）的捕获，并将错误输出到 `std::cerr`。
  * **`make_sure_log_file()` / `close_log_file()` (在 `main.cpp` 中使用)**:
    * `make_sure_log_file()`: 本质上是调用 `Logger::getInstance()` 来确保单例被创建和初始化。
    * `close_log_file()`: 调用 `Logger::getInstance().flush()`。实际的关闭和线程join发生在`Logger`的析构函数中（通常是程序退出时全局对象析构）。

### 5. `frontend-defs.hpp` & `cloud-compile-frontend.hpp`

* **`frontend-defs.hpp`**:
  * **作用**: 集中定义项目中使用的全局宏常量、预设值和应用基本信息。
  * **内容示例**:
    * `FILENAME_BUFFER_SIZE`, `ERROR_BUFFER_SIZE`, `REGULAR_BUFFER_SIZE`: 用于日志或其他地方的字符缓冲区大小建议。
    * `GLOBAL_THREAD_LIMITATION`: 可能是计划给 `ThreadPool` 的一个限制，但当前 `ThreadPool` 默认使用硬件并发数。
    * `DEFAULT_PORT`, `SERVER_IP`: 后端服务器的默认连接参数。
    * `LOG_DIRECTORY`, `OUT_DIRECTORY`: 日志和（可能的）输出文件的相对目录名。
    * `APP_VERSION`: 前端应用程序的版本号，用于窗口标题等。
* **`cloud-compile-frontend.hpp`**:
  * **作用**: 作为前端项目的一个主要的“umbrella header”或聚合头文件。它包含了许多常用标准库的头文件，以及项目内部关键模块的头文件 (如 `network/network.hpp`, `frontend-defs.hpp`)。
  * **目的**: 简化其他 `.cpp` 文件中的 `#include` 指令，提供一个统一的包含点。
  * 也包含了对日志系统API函数（如 `make_sure_log_file`, `close_log_file`, `runMainWindow`）的前向声明或`extern`声明（如果适用）。

## 🖥️ 环境要求与依赖 (Environment & Dependencies)

1. **C++ 编译器**: 需要支持C++20。
2. **Qt Framework**: Qt 6.9.0 是必需的。
    * 核心模块: Qt Core, Qt GUI, Qt Widgets, Qt Network, Qt Concurrent。
3. **构建系统**:
4. **操作系统**: 由于使用了 Qt，理论上具有跨平台能力。但网络部分 (`ClientSocket`) 的底层实现基于 POSIX sockets (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `poll`, `fcntl` 等)，在 Windows 上可能需要额外处理或依赖 Qt 的网络抽象 (尽管看起来是直接用了POSIX API)。
5. **后端服务器**: 需要 Simple-K Cloud Executor 后端服务器正在运行，并监听在可访问的IP和端口 (默认为 `127.0.0.1:3040`)。
