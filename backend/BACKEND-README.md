# Simple-K Cloud Executor - 后端项目文档

欢迎来到 Simple-K Cloud Executor 的后端！这是一个基于C++20构建的高性能、事件驱动的TCP服务器，专为处理计算密集型任务（如云端编译和执行）而设计。本项目采用模块化设计，易于扩展和维护。

## 📝 项目概览

Simple-K Cloud Executor 后端旨在提供一个稳定、高效的平台，允许客户端通过TCP连接提交源代码，服务器后端负责编译这些代码，执行生成的可执行文件，并将结果返回给客户端。它利用了现代C++的特性以及Linux系统调用的强大功能，以实现高并发和低延迟。

**核心特性:**

* **事件驱动架构 (Event-Driven Architecture)**: 基于 `epoll` 的事件循环 (`EventLoop`)，实现高效的I/O多路复用 (I/O Multiplexing)。
* **非阻塞I/O (Non-blocking I/O)**: 所有网络操作均为非阻塞，确保服务器在高负载下依然响应迅速。
* **多线程处理 (Multithreading)**: 使用线程池 (`ThreadPool`) 异步处理耗时的业务逻辑（如编译和执行），避免阻塞事件循环。
* **自定义协议 (Custom Protocol)**: 实现了简单的自定义应用层协议，用于客户端和服务器之间的通信，支持消息标签和动态长度的负载。
* **模块化设计 (Modular Design)**: 清晰的类和模块划分，如网络层、业务逻辑层、日志系统等。
* **云端编译与执行 (Cloud Compilation & Execution)**: 核心功能是接收代码、编译并在沙箱化（概念上，具体实现依赖 `compile-thread.cpp`）环境中执行，返回结果。
* **异步日志系统 (Asynchronous Logging System)**: 高效的异步日志记录，将日志写入文件，便于问题排查和性能分析。

## 📁 项目结构与核心组件

backend/
├── main.cpp                     # 程序入口，初始化服务器并启动事件循环
├── compile-thread.cpp           # 包含编译和执行外部命令的核心逻辑
├── write-log.cpp                # 实现异步日志记录功能
├── network/                     # 网络层核心代码目录
│   ├── network.hpp              # 网络层主要头文件，包含所有网络相关类的声明和通用工具
│   ├── class.EventLoop.cpp      # EventLoop 类的实现
│   ├── class.Channel.cpp        # Channel 类的实现
│   ├── class.Acceptor.cpp       # Acceptor 类的实现
│   ├── class.TcpConnection.cpp  # TcpConnection 类的实现
│   ├── class.TcpServer.cpp      # TcpServer 类的实现
│   └── class.Buffer.cpp         # Buffer 类的实现
├── cloud-compile-backend.hpp    # 项目主要的后端头文件，聚合了常用头文件和全局声明
└── backend-defs.hpp             # 定义了项目中使用的一些常量和枚举

### 1. `main.cpp` - 程序入口与服务引导

* **作用**:
  * 作为整个后端应用程序的起点。
  * 创建并初始化 `EventLoop` 对象和 `TcpServer` 对象。
  * 注册服务器支持的协议处理器，例如 "compile-execute" 和 "Hello" 协议。
  * 设置服务器的连接回调函数，用于处理新连接建立和断开的事件。
  * 启动服务器的监听和事件循环。
* **大致原理**:
  * `main` 函数首先创建一个 `EventLoop` 实例，这是整个服务器事件驱动模型的核心。
  * 接着，创建一个 `TcpServer` 实例，将 `EventLoop` 传递给它，并指定监听的端口号。
  * 通过 `server.register_protocol_handler()` 方法，将特定的字符串标签（如 "compile-execute"）与一个处理该协议的lambda函数关联起来。这些lambda函数负责解析特定协议的请求并生成响应。
  * `server.start()` 会启动 `Acceptor` 开始监听新的连接请求。
  * `loop.loop()` 会启动事件循环，`EventLoop` 开始阻塞等待I/O事件。
  * 还包含一个全局的 `global` 结构体实例，其构造函数负责在程序启动时创建必要的目录（如 `src`, `out`, `cpl-log`）并初始化日志系统；析构函数负责在程序退出时关闭日志文件。

### 2. `network/` 核心网络类

#### 2.1. `EventLoop` 类 (`class.EventLoop.cpp`, `network.hpp`)

* **作用**:
  * 实现Reactor模式中的事件分发器 (Demultiplexer) 和事件处理器 (Dispatcher) 的角色。
  * 每个 `EventLoop` 对象通常在一个线程中运行（"IO线程"），负责监听和分发该线程中所有文件描述符 (FD) 上的I/O事件。
* **大致原理**:
  * 内部封装了一个 `epoll` 文件描述符。通过 `epoll_create1` 创建。
  * 使用一个 `Channel` 对象 (`wakeup_channel_`) 和 `eventfd` 来实现跨线程唤醒机制，允许其他线程安全地将任务添加到此 `EventLoop` 的任务队列中。
  * `loop()` 方法是事件循环的核心，它会调用 `epoll_wait` 阻塞等待事件发生。
  * 当事件发生时，`epoll_wait` 返回，`EventLoop` 遍历就绪的 `Channel`，并调用其 `handle_event()` 方法。
  * `run_in_loop()` 和 `queue_in_loop()` 方法用于确保传递给它们的函数（`Functor`）总是在 `EventLoop` 所在的IO线程中执行。如果调用者不在IO线程，任务会被放入一个队列，并通过 `wakeup()` 唤醒IO线程来执行。
  * 管理 `Channel` 对象的注册 (`update_channel`)、移除 (`remove_channel`)。

#### 2.2. `Channel` 类 (`class.Channel.cpp`, `network.hpp`)

* **作用**:
  * 封装了一个文件描述符 (FD) 及其关注的事件类型（如可读、可写）以及事件发生时的回调函数。
  * 它是 `EventLoop` 和具体I/O事件处理逻辑之间的桥梁。每个 `Channel` 对象只属于一个 `EventLoop`。
* **大致原理**:
  * 每个 `Channel` 对象都持有一个文件描述符 (`fd_`) 和一个指向其所属 `EventLoop` 的指针。
  * 通过 `enable_reading()`, `enable_writing()`, `disable_all()` 等方法可以设置或清除关注的事件类型 (EPOLLIN, EPOLLOUT 等)，并通过调用 `update()` 方法通知 `EventLoop` 更新 `epoll` 对此FD的监听状态。
  * 当 `EventLoop` 检测到此 `Channel` 对应的FD上有事件发生时，会调用其 `handle_event()` 方法。
  * `handle_event()` 根据发生的具体事件类型 (`revents_`) 调用预先注册的读回调 (`read_cb_`)、写回调 (`write_cb_`) 或错误回调 (`error_cb_`)。
  * `tie()` 方法用于解决 `TcpConnection` 和 `Channel` 之间的生命周期管理问题，确保在执行回调时，相关的 `TcpConnection` 对象仍然存活。

#### 2.3. `Socket` 类 (`network.hpp`)

* **作用**:
  * 一个简单的RAII (Resource Acquisition Is Initialization) 封装类，用于管理套接字文件描述符。
* **大致原理**:
  * 构造时接收一个文件描述符，析构时自动调用 `::close()` 关闭该文件描述符，防止资源泄漏。
  * 提供了 `fd()` 方法获取原始文件描述符，以及 `release()` 方法转移文件描述符的所有权。
  * 支持移动构造和移动赋值，符合现代C++的资源管理最佳实践。

#### 2.4. `Acceptor` 类 (`class.Acceptor.cpp`, `network.hpp`)

* **作用**:
  * 专门用于服务器端，负责监听新的TCP连接请求。
  * 当有新连接到达时，接受 (accept) 它，并调用上层（`TcpServer`）注册的回调函数来处理这个新连接。
* **大致原理**:
  * 构造时创建一个监听套接字 (`accept_socket_`)，通过 `::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP)` 创建，确保了套接字在创建时即为非阻塞且在 `exec` 系列函数调用时自动关闭。
  * 设置套接字选项 `SO_REUSEADDR` 和 `SO_REUSEPORT` (如果 `reuse_port` 为 `true`)，允许快速重启服务器并重用端口。
  * 将监听套接字绑定 (`::bind`)到指定端口和 `INADDR_ANY` (监听所有网络接口)。
  * 拥有一个 `Channel` (`accept_channel_`)，该 `Channel` 关注监听套接字上的可读事件 (EPOLLIN)。`accept_channel_` 的读回调设置为 `Acceptor::handle_read()`。
  * `listen()` 方法开始在监听套接字上调用 `::listen()` (参数 `SOMAXCONN` 表示系统推荐的最大连接队列长度)，并使能 `accept_channel_` 的读事件。
  * 当有新连接请求到达时，`accept_channel_` 的读回调 `handle_read()` 会被触发。
  * `handle_read()` 内部循环调用 `::accept4()` 来接受所有等待的连接。`accept4` 可以直接将新接受的连接套接字设置为非阻塞和 `SOCK_CLOEXEC`，简化了流程。
  * 对于每个成功接受的连接，如果注册了 `new_connection_cb_` (由 `TcpServer` 设置)，则调用该回调，并将新连接的文件描述符和对端地址传递过去。如果未设置回调，则会记录警告并关闭该连接。
  * 包含一个 `idle_fd_` (打开 `/dev/null`)，这是一个处理"文件描述符耗尽" (EMFILE/ENFILE错误) 的经典技巧：当 `accept4` 因FD耗尽失败时，先关闭 `idle_fd_`，然后调用 `accept` (此时会成功，因为有一个FD空出来了)，立即关闭这个刚接受的连接（不处理它），再重新打开 `idle_fd_`。这样可以避免服务器因无法接受新连接而完全卡死，并记录错误。

#### 2.5. `Buffer` 类 (`class.Buffer.cpp`, `network.hpp`)

* **作用**:
  * 一个用于网络I/O的缓冲区类，提供了方便的接口来读取和写入数据。
  * 它内部维护一个 `std::vector<char>` 作为实际的存储空间，并管理读写指针，以实现高效的数据处理。
* **大致原理**:
  * **内存布局**: 采用 "prependable bytes + readable bytes + writable bytes" 的设计。
    * `kCheapPrepend` (默认为8字节): 缓冲区头部预留一小块空间，通常用于方便地在消息前添加长度等头部信息，而无需移动现有数据。
    * `reader_index_`: 指向可读数据的起始位置。
    * `writer_index_`: 指向可写区域的起始位置（也是已写入数据的末尾）。
  * **空间管理 (`make_space(len)`)**: 当需要写入 `len` 字节但可写空间不足时调用。
    * 如果 `writable_bytes() + prependable_bytes()` (即总的未使用空间) 小于 `len + kCheapPrepend` (需要的空间加上头部预留)，则直接 `buffer_.resize(writer_index_ + len)` 扩展整个 `vector`。
    * 否则，说明通过移动现有可读数据到缓冲区头部可以腾出足够空间。此时，使用 `memmove` 将 `readable_bytes()` 的数据从 `peek()` (即 `begin() + reader_index_`) 移动到 `begin() + kCheapPrepend`。然后更新 `reader_index_` 和 `writer_index_`。
  * **读操作**:
    * `peek()`: 返回指向可读数据起始位置的 `const char*`。
    * `retrieve(len)`: 将 `reader_index_` 向前移动 `len` 字节，表示这部分数据已被消耗。如果消耗所有可读数据，则调用 `retrieve_all()`。
    * `retrieve_all()`: 将 `reader_index_` 和 `writer_index_` 都重置为 `kCheapPrepend`，清空缓冲区。
    * `retrieve_as_string(len)`: 取出 `len` 字节数据作为 `std::string` 并消耗掉。
  * **写操作**:
    * `append(data, len)`: 将 `data` 指向的 `len` 字节数据追加到缓冲区末尾。它会先调用 `ensure_writable_bytes(len)` 确保空间，然后 `memcpy` 数据，最后调用 `has_written(len)` 更新 `writer_index_`。
  * **从FD读取数据 (`read_fd(fd, saved_errno)`)**:
    * 这是一个关键的高效读取方法，使用了 `readv` (分散读) 系统调用。
    * 它准备了两个 `iovec` 结构：
            1. 第一个 `iovec` 指向 `Buffer` 内部当前的可写空间 (`begin_write()`, `writable_bytes()`)。
            2. 第二个 `iovec` 指向栈上的一个临时大缓冲区 `extrabuf` (大小为65536字节)。
    * 调用 `::readv(fd, vec, 2)`，尝试一次性将数据读入这两个区域。
    * **处理读取结果 `n`**:
      * 如果 `n < 0`，发生错误，保存 `errno` 并返回-1。
      * 如果 `n == 0`，表示对端关闭连接 (EOF)，返回0。
      * 如果 `static_cast<size_t>(n) <= writable` (即读取的数据量小于等于当前内部可写空间)，则直接调用 `has_written(n)` 更新 `writer_index_`。
      * 如果 `static_cast<size_t>(n) > writable` (即内部可写空间不够，数据被读入了 `extrabuf`)：
        * 检查是否数据溢出（`n` 大于两个缓冲区的总和），如果是则记录错误。
        * 将 `writer_index_` 更新到 `buffer_.size()` (因为第一个 `iovec` 已被填满)。
        * 计算需要从 `extrabuf` 中追加的数据量 `must_write_from_extrabuf`。
        * 调用 `append(extrabuf, must_write_from_extrabuf)` 将 `extrabuf` 中的数据追加到 `Buffer` (此时 `append` 内部的 `ensure_writable_bytes` 会负责扩容)。
    * 这种使用 `readv` 和栈上临时缓冲区的策略，既能充分利用 `Buffer` 内部空间，又能处理一次读取大量数据的情况，而无需预先猜测并分配一个巨大的 `Buffer`，是一种常见的性能优化手段。

#### 2.6. `TcpConnection` 类 (`class.TcpConnection.cpp`, `network.hpp`)

* **作用**:
  * 表示一个TCP连接。服务器为每个接受的客户端连接创建一个 `TcpConnection` 对象。
  * 负责处理该连接上的数据收发、连接状态管理以及生命周期管理。
* **大致原理**:
  * 继承自 `std::enable_shared_from_this`，以便安全地获取自身的 `shared_ptr` 传递给回调函数，确保在异步操作中对象依然存活。
  * 拥有一个 `Socket` 对象（表示连接的套接字）和一个 `Channel` 对象（用于在 `EventLoop` 中注册此连接套接字的I/O事件）。
  * 管理连接的状态 (`State::kConnecting`, `State::kConnected`, `State::kDisconnecting`, `State::kDisconnected`)。
  * **数据接收**: 当其 `Channel` 报告可读事件时，`handle_read()` 被调用。它使用 `input_buffer_` (一个 `Buffer` 对象) 的 `read_fd()` 方法从套接字读取数据。读取到数据后，调用 `message_cb_` (消息回调，由 `TcpServer` 设置) 处理。
    * `message_cb_` 的处理被放到了 `ThreadPool::instance().enqueue()` 中执行，以避免阻塞IO线程。lambda捕获了 `this` 和 `self` (一个 `shared_ptr` 副本) 以及 `input_buffer_` 的指针。
  * **数据发送**: `send()` 方法供上层调用。它会将数据放入 `output_buffer_`。如果当前没有正在发送的数据且IO线程安全，会尝试直接 `::write()`。如果不能立即发送或只发送了一部分，会启用其 `Channel` 的可写事件。当 `Channel` 报告可写事件时，`handle_write()` 被调用，它会从 `output_buffer_` 中取出数据写入套接字，直到数据全部发送完毕或套接字不可写。
  * **连接管理**:
    * `connect_established()`: 在新连接建立时由 `TcpServer` 调用，设置状态为 `kConnected`，启用读事件，并调用 `connection_cb_`。它还会调用 `channel_->tie(shared_from_this())` 来将 `Channel` 与 `TcpConnection` 的生命周期绑定。
    * `connect_destroyed()`: 在连接关闭后（无论是正常关闭还是错误关闭）调用，清理资源 (如从 `EventLoop` 中移除 `Channel`)，并再次调用 `connection_cb_`（如果连接曾成功建立）。
    * `shutdown()`: 优雅关闭连接（发送FIN包）。它会设置状态为 `kDisconnecting`，并在IO线程中调用 `shutdown_in_loop()`。如果输出缓冲区中没有数据，`shutdown_in_loop()` 会直接调用 `::shutdown(socket_.fd(), SHUT_WR)`；否则，会等待数据发送完毕后再关闭。
    * `force_close()`: 强制关闭连接。设置状态为 `kDisconnecting`，并在IO线程中调用 `force_close_in_loop()`，后者直接调用 `handle_close()`。
  * **回调机制**: 提供了多种回调函数接口 (`connection_cb_`, `message_cb_`, `write_complete_cb_`, `high_water_mark_cb_`, `close_cb_`)，供上层定制连接行为。`high_water_mark_cb_` 用于在输出缓冲区数据量超过阈值时通知上层，进行流量控制。

#### 2.7. `TcpServer` 类 (`class.TcpServer.cpp`, `network.hpp`)

* **作用**:
  * 服务器端的核心类，用于管理监听、接受新连接、以及管理所有 `TcpConnection` 对象。
  * 提供接口注册协议处理器和事件回调。
* **大致原理**:
  * 拥有一个 `EventLoop` 指针（通常是主 `EventLoop`）和一个 `Acceptor` 对象（用于接受新连接）。
  * `start()` 方法会启动 `Acceptor` 开始监听。它通过 `loop_->run_in_loop()` 确保 `acceptor_->listen()` 在正确的IO线程中执行。
  * 当 `Acceptor` 接受一个新连接时，会调用 `TcpServer` 的 `new_connection()` 方法。
  * `new_connection()`:
        1. 为新连接生成一个唯一的名称。
        2. 创建一个 `TcpConnection` 对象 (`std::make_shared`)。
        3. 为这个 `TcpConnection` 对象设置各种回调函数：
            *`connection_cb_`: 用户设置的连接建立/断开回调。
            * `message_cb_`: 设置为 `TcpServer::on_message`，用于处理接收到的数据。
            *`write_complete_cb_`: 用户设置的写完成回调。
            * `close_cb_`: 设置为 `TcpServer::remove_connection`，用于在连接关闭时清理。
        4. 将新创建的 `TcpConnection` 对象存入一个 `std::unordered_map<string, TcpConnectionPtr>` (`connections_`) 中进行管理。
        5. 在IO线程中调用 `conn->connect_established()` 来完成连接的初始化。
  * `on_message()`: 这是 `TcpConnection` 的 `message_cb_`。当连接上有数据可读时，此方法被调用。它负责解析 `Buffer` 中的数据，识别协议标签和负载长度，然后分发给相应的协议处理器。
    * **协议解析**: `TcpServer` 实现了一个简单的应用层协议：`[1-byte tag_len][tag_string][4-byte payload_len_network_order][payload_data]`。
      * `tag_len`: 标签字符串的长度。
      * `tag_string`: 协议标签。
      * `payload_len_network_order`: 负载数据的长度，网络字节序。
      * `payload_data`: 实际的负载数据。
    * `attempt_protocol_processing()`: 循环尝试按上述新协议格式从 `Buffer` 中解析完整的消息帧。
      * 检查是否有足够数据读取 `tag_len`。
      * 检查 `tag_len` 是否在合理范围 (非0且小于64)。
      * 检查是否有足够数据读取 `tag_string` 和 `payload_len_network_order`。
      * 读取并转换 `payload_len` (使用 `ntohl`)，检查是否超过 `kMaxPayloadSize`。
      * 检查是否有足够数据读取完整的 `payload_data`。
    * **处理器分发**:
      * 如果解析出一个完整的协议帧，提取 `tag`。
      * 首先查找 `protocol_handlers_` (新的协议处理器映射) 中是否有匹配的处理器。如果有，调用 `execute_protocol_handler()`。
      * 如果新的处理器未找到，尝试在 `handlers_` (旧的协议处理器映射) 中查找。如果找到，调用 `execute_legacy_handler_for_tag()`。这里体现了对旧版处理器的兼容性考虑。
      * 如果上述都未匹配，但数据帧符合新协议格式，则调用 `default_protocol_handler_` (默认的新协议处理器)。
      * 如果数据根本不符合新协议格式 (`attempt_protocol_processing` 返回 `false`)，则调用 `process_legacy_fallback()`，它会尝试使用 `default_handler_` (旧的默认处理器) 处理整个缓冲区。
    * `execute_protocol_handler()` / `execute_default_protocol_handler()`: 调用相应的处理器lambda，传递连接、标签和负载 (`string_view`)。处理器返回 `ProtocolHandlerPair` (`{response_tag, response_payload_string}`)。服务器使用 `TcpServer::package_message()` 将响应打包并发送。
    * `execute_legacy_handler_for_tag()`: 调用旧的处理器lambda，它直接操作 `Buffer` 并返回一个 `std::string` 作为响应。
  * `register_protocol_handler()`: 注册基于 `ProtocolHandlerPair` 返回值的新协议处理器。
  * `set_default_protocol_handler()`: 设置默认的新协议处理器。
  * `register_handler()`: 注册基于 `std::string` 返回值的旧协议处理器。
  * `set_default_handler()`: 设置默认的旧协议处理器。
  * `package_message()`: 一个静态辅助方法，用于将标签和负载打包成服务器定义的协议格式。
  * `remove_connection()`: 当连接关闭时，在IO线程中调用 `remove_connection_in_loop()`，将对应的 `TcpConnection` 从 `connections_` 映射中移除，并确保其 `connect_destroyed()` 被调用。

### 3. `compile-thread.cpp` - 编译与执行模块

* **作用**:
  * 封装了执行外部命令（特别是编译器如 `g++` 和用户编译生成的可执行文件）的逻辑。
  * 负责重定向子进程的标准输入、标准输出和标准错误。
* **大致原理**:
  * **`FdGuard` 类**: 一个RAII类，用于管理文件描述符，确保在作用域结束时自动关闭，类似于 `Socket` 类但更通用。
  * **`compile_files(instructions)` 函数**:
        1. 接收一个字符串向量作为编译指令（例如 `{"g++", "source.cpp", "-o", "output.out"}`）。
        2. 使用 `pipe2()` (或 `pipe()` + `fcntl()`) 创建一个管道，用于捕获编译器的标准错误输出 (stderr)。`O_CLOEXEC` 标志确保管道在 `exec` 时关闭。
        3. `fork()` 创建子进程。
        4. **子进程**:
            *关闭管道的读端。
            * 使用 `dup2()` 将管道的写端重定向到 `STDERR_FILENO`。
            *调用 `execvp()` 执行编译器命令。如果 `execvp` 失败，子进程会 `_exit(EXIT_FAILURE)`。
        5. **父进程**:
            * 关闭管道的写端。
            *从管道的读端循环读取数据 (`read()`)，直到遇到EOF，从而捕获所有编译器的stderr输出，并存入 `stringstream`。
            * 调用 `waitpid()` 等待子进程结束，并检查其退出状态 (`WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG`)。
            * 返回捕获到的stderr字符串。这个字符串的 `length()` 是否为0可以作为是否有编译错误或警告的一个初步判断依据，但更可靠的是检查 `WEXITSTATUS(child_status)` 是否为0以及目标文件是否生成。
  * **`execute_executable(command_line, input_filename)` 函数**:
        1. 接收可执行文件的路径和参数，以及一个可选的输入文件名（用于重定向到子进程的stdin）。
        2. 生成唯一的输出文件名 (`*.output`) 和错误文件名 (`*.err`)，通常基于时间戳，存放在 `out/` 目录下。
        3. 使用 `open()` 打开这三个文件（如果提供了输入文件），获取它们的文件描述符，并使用 `FdGuard` 管理。`O_CLOEXEC` 标志确保这些FD在 `exec` 时关闭。
        4. `fork()` 创建子进程。
        5. **子进程**:
            *根据需要，使用 `dup2()` 将标准输入 (`STDIN_FILENO`)、标准输出 (`STDOUT_FILENO`)、标准错误 (`STDERR_FILENO`) 重定向到相应的文件描述符。
            * 调用 `execvp()` 执行用户程序。如果 `execvp` 失败，子进程会 `_exit(EXIT_FAILURE)`。
        6. **父进程**:
            *调用 `waitpid()` 等待子进程结束，并记录子进程的退出状态。
            * 返回一个元组：一个布尔值表示执行是否出错（例如 `fork` 失败、`waitpid` 失败或子进程非正常退出），以及生成的输出文件名和错误文件名。
                * 如果布尔值为 `true`，则元组的第二个字符串通常包含错误信息；如果为 `false`，则第二和第三个字符串是 `.output` 和 `.err` 文件的路径。

### 4. `write-log.cpp` - 异步日志系统

* **作用**:
  * 提供一个高效的、线程安全的异步日志记录功能。
  * 应用程序通过调用 `log_write_xxx_information()` 函数来记录日志，这些日志消息会被放入一个队列，由一个专门的后台线程负责写入文件。
* **大致原理**:
  * **`Logger` 类 (单例)**:
        1. 使用单例模式 (`getInstance()`) 确保全局只有一个 `Logger` 实例。构造函数是私有的。
        2. 构造函数中：
            *根据当前时间生成日志文件名，例如 `cpl-log/cpl-back-timestamp.log`。
            * 打开日志文件 (`std::ofstream log_file_`)。如果打开失败，会抛出 `std::runtime_error`，并且 `is_initialized_` 标志不会被设置为 `true`。
            *创建一个后台工作线程 (`writer_thread_`)，该线程运行 `Logger::worker()` 方法。
            * `is_initialized_` (atomic) 标志在成功初始化后设为 `true`。
        3. 析构函数中：设置 `shutdown_requested_` (atomic) 标志为 `true`，通知工作线程退出，`cv_.notify_all()` 唤醒可能等待的线程，然后 `join()` 工作线程。最后关闭日志文件。
        4. `enqueueLog(log_entry)`: 供外部调用的接口。如果日志系统未初始化或已请求关闭，则直接返回。否则，加锁 (`queue_mutex_`) 将格式化好的日志条目（`std::string`）放入一个 `std::queue<string>` (`log_queue_`)，并使用条件变量 (`cv_`) 通知工作线程有新的日志需要处理。
  * **`worker()` 方法 (运行在 `writer_thread_` 中)**:
        1. 在一个循环中运行。
        2. 使用 `std::unique_lock` 和条件变量 `cv_` 等待，直到 `log_queue_` 非空或 `shutdown_requested_` 为 `true`。
        3. 当被唤醒时：
            *如果已请求关闭且队列为空，则退出循环。
            * 如果队列中有日志，则将主队列 (`log_queue_`) 中的所有日志条目 `swap` 到一个局部队列 (`local_queue`) 中（这样做是为了尽快释放锁，减少主队列被锁定的时间）。
            *解锁。
            * 遍历局部队列，将每条日志写入文件流 (`log_file_ << ... << '\n'`)。如果写入失败 (`log_file_.good()` 为 `false`)，则输出错误到 `std::cerr`。
            * 写入局部队列后，`log_file_.flush()` 确保数据写入磁盘。
        4. 循环结束后（准备退出线程时），再次 `flush()` 日志文件。
  * **`formatLogMessage(level, information)`**: 一个辅助函数，用于将日志级别、精确到微秒的时间戳和日志信息格式化成统一的字符串。使用 `std::put_time` 和 `std::chrono`。
  * **`log_write_error_information()`, `log_write_regular_information()`, `log_write_warning_information()`**: 对外暴露的日志记录接口，它们内部调用 `formatLogMessage` 和 `Logger::getInstance().enqueueLog()`。它们会捕获 `Logger::getInstance()` 可能抛出的 `std::runtime_error` (如果日志系统未成功初始化)。
  * **`make_sure_log_file()` 和 `close_log_file()`**:
    * `make_sure_log_file()`: 调用 `Logger::getInstance()` 来触发单例的初始化。如果初始化失败，异常会传播出去。
    * `close_log_file()`: 调用 `Logger::getInstance().flush()` 来确保所有缓冲的日志在程序退出前被写入。真正的关闭文件操作是在 `Logger` 的析构函数中完成的，这里只是确保刷新。

### 5. `cloud-compile-backend.hpp` 和 `backend-defs.hpp`

* **`backend-defs.hpp`**:
  * **作用**: 定义了项目中广泛使用的常量宏和枚举类型。
  * **内容示例**:
    * `enum class ThreadStatCode`: 定义了线程操作可能返回的状态码。
    * `FILENAME_BUFFER_SIZE`, `ERROR_BUFFER_SIZE`, `REGULAR_BUFFER_SIZE`: 定义了各种缓冲区的预设大小。
    * `GLOBAL_THREAD_LIMITATION`: 线程池的最大线程数限制 (这个宏未被直接使用)。
    * `DEFAULT_PORT`: 服务器默认监听的端口号。
    * `LOG_DIRECTORY`, `OUT_DIRECTORY`: 日志文件和输出文件的存放目录名。
* **`cloud-compile-backend.hpp`**:
  * **作用**: 作为一个主要的聚合头文件，它包含了许多其他标准库头文件和项目内部头文件（如 `network/network.hpp` 和 `backend-defs.hpp`）。
  * **内容示例**:
    * Includes: `iostream`, `list`, `fstream`, `filesystem`, `sys/wait.h` 等。
    * `extern` 声明: 声明了一些全局变量，如 `global_thread_pool` (`ThreadPool` 现在通过单例 `ThreadPool::instance()` 访问，这个 `extern` 可能不再需要或用途已改变)，以及与旧版日志系统相关的 `log_mutex`, `log_file` (新的日志系统是 `write-log.cpp` 中的 `Logger` 类，这些 `extern` 可能也已废弃)。
    * 函数前向声明: `make_sure_log_file()` 和 `close_log_file()`。

## 环境要求

1. C++20 兼容的编译器 (如 GCC 10+ 或 Clang 10+)。
2. Linux 操作系统 (因为用到了 `epoll`, `eventfd`, `fork`, `pipe2`, `accept4` 等Linux特有的API)。
3. CMake (用于构建项目)。

## 未来展望与可扩展点

* **安全性增强**:
  * 实现更严格的沙箱环境来执行用户代码，限制其系统调用、文件访问和网络访问权限。
  * 考虑使用TLS/SSL对通信进行加密。
* **资源管理**:
  * 对客户端提交的任务进行更细致的资源限制（CPU时间、内存使用、编译时间等）。
  * 实现更完善的队列管理和优先级调度。
* **更丰富的协议**:
  * 支持文件上传/下载、用户认证等。
* **配置化**:
  * 将端口号、线程池大小、目录路径等配置项外部化到配置文件中。
