# Simple-K Cloud Executor 🚀

> ✨ 一款基于现代 C++20 和 Qt 6.9.0精心打造的轻量级、高性能云端任务执行器 ✨

**Simple-K Cloud Executor** 是一个包含功能强大的后端服务器和与之配套的直观图形化前端的完整解决方案。它旨在提供一个稳定、高效的平台，允许用户通过网络提交任务（如源代码），由后端进行编译和执行，并将结果安全可靠地返回给用户。本项目充分利用了现代C++的卓越性能与Qt框架的跨平台能力，为您带来流畅与高效的云端计算体验。

## 核心亮点 (Key Qualities & Highlights) 🌟

* **现代化技术栈**: 全面拥抱 **C++20** 标准，利用其最新特性提升代码质量和执行效率。前端界面采用 **Qt 6.9.0** 构建，保证了美观性、响应速度和跨平台潜力。
* **高性能事件驱动后端 (High-Performance Event-Driven Backend)**: 后端服务器采用基于 `epoll` (Linux) 的事件循环，实现高并发I/O处理和非阻塞网络操作，确保在高负载下依然能快速响应。
  * *后端是真正的性能怪兽哦！💪*
* **异步化与多线程 (Asynchronous & Multithreaded)**:
  * **后端**: 使用线程池异步处理耗时的业务逻辑（如编译、执行用户代码），避免阻塞核心事件循环，最大化吞吐量。
  * **前端**: 网络通信和部分文件操作采用异步设计，`ClientSocket` 通过独立线程处理收发，`TaskManager` 利用 `QtConcurrent` 处理后台任务，确保UI始终流畅不卡顿。
* **安全稳健的通信 (Secure & Robust Communication)**: 前后端之间采用自定义的TCP应用层协议，通过明确的消息标签和长度定义，保证数据传输的完整性和可靠性。
* **直观易用的前端 (Intuitive Frontend GUI)**: 
  * 提供友好的图形用户界面，轻松浏览本地文件、选择任务并提交至云端。
  * 实时任务状态追踪，让用户对文件上传、服务器处理及结果下载了如指掌。
* **高效的异步日志系统 (Efficient Asynchronous Logging)**: 前后端均配备了独立的异步日志系统，能够将详细的运行时信息、警告及错误记录到本地文件，极大地方便了问题排查和性能分析，同时对主程序性能影响降至最低。
* **模块化与可扩展设计 (Modular & Extensible Design)**: 代码结构清晰，前后端均采用模块化设计，无论是网络层、UI层还是业务逻辑层，都易于理解、维护和扩展新功能。
* **清晰的构建体系 (Clear Build System)**: 采用 **CMake (≥ 3.16)** 作为统一的构建系统管理工具，支持多种构建类型（Debug, Release, NativeOptimizationRelease ），并为开发者提供了便捷的编译脚本。

## 快速上手 (Quick Start) 🛠️

提供了便捷的 `bash` 脚本来帮助你快速编译整个 Simple-K Cloud Executor 项目（包括前端和后端）!

**环境要求 (Prerequisites):**

* **C++20 兼容编译器**: 如 GCC (推荐版本 ≥ 10) 或 Clang (推荐版本 ≥ 10)。
* **CMake**: 版本 ≥ 3.16 (根据 `CMakeLists.txt` )。
* **Qt**: 版本 **6.9.0** 。确保已正确安装，并且CMake能够找到它。脚本中默认的 `CMAKE_PREFIX_PATH` 指向 `~/Documents/QT/6.9.0/gcc_64`，请根据你的实际Qt安装路径进行调整。
  * 前端依赖的 Qt 模块: `Core`, `Gui`, `Widgets`, `Concurrent` 。
* **Linux 环境**: 后端服务器使用了Linux特有的API (如 `epoll`)。前端因直接使用POSIX socket API也推荐在Linux或类Unix环境编译运行，或确保有相应的兼容层。

**编译步骤 (Build Steps):**

1. **克隆项目 (Clone the Repository)**:

    ```bash
    git clone https://github.com/kleedaisuki/Desire-Cloud-Computing Simple-K-Cloud-Executor
    cd Simple-K-Cloud-Executor
    ```

2. **调整构建脚本中的Qt路径 (Adjust Qt Path in Build Scripts - IF NEEDED)**:
    打开 `make.sh` (用于Release构建) 或 `dmake.sh` (用于Debug构建)，找到以下行：

    ```bash
    CMAKE_PREFIX_PATH="~/Documents/QT/6.9.0/gcc_64"
    ```

    如果你的 Qt 6.9.0 安装路径不同，请将其修改为你的实际路径。例如：`/opt/Qt/6.9.0/gcc_64` 或 `/usr/local/Qt-6.9.0/lib/cmake/Qt6`。

3. **执行构建脚本 (Execute the Build Script)**:

    * **推荐的 Release 构建 (Optimized for performance):**

        ```bash
        chmod +x make.sh
        ./make.sh
        ```

        此脚本会创建一个 `build` 目录，在其中进行CMake配置和编译，并将编译日志输出到 `build/compilation-[时间戳].log` 文件中。

    * **Debug 构建 (For development and debugging):**

        ```bash
        chmod +x dmake.sh
        ./dmake.sh
        ```

        同样，编译日志会记录在 `build/compilation-[时间戳].log`。

4. **运行程序 (Run the Applications)**:
    编译成功后，你会在 `build` 目录下找到两个主要的可执行文件：
    * `back.exe`: 后端服务器。
    * `front.exe`: 前端图形界面客户端。 

    你需要先运行后端服务器，然后再启动前端客户端。

    ```bash
    cd build
    ./back.exe  # 启动后端服务器 (通常会在后台运行或需要新终端)
    ./front.exe # 在另一个终端或直接启动前端GUI
    ```

    前端程序将尝试连接到在 `frontend-defs.hpp` 中定义的默认服务器地址 (`127.0.0.1`) 和端口 (`3040`)。

## 关于许可证的重要说明 (Important License Information) 📜

本项目 **Simple-K Cloud Executor** 的源代码（包括其前端 `front.exe` 和后端 `back.exe` 组件）是基于 **GNU General Public License v3.0 or later (GPL-3.0-or-later)** 进行许可的。

* 这意味着你有权自由地运行、研究、分享和修改本软件。
* 如果你分发了本软件的修改版本，或任何基于本软件创建的衍生作品，那么这些作品也必须在相同的 GPL-3.0-or-later 许可下提供其对应的完整源代码。
* 完整的许可证条款，请仔细阅读项目根目录下的 `LICENSE` 文件。

**第三方库依赖 (Third-Party Library Dependencies):**

本项目的**前端组件 (`front.exe`)** 依赖于 **Qt Framework**，具体版本为 **6.9.0**。

* **Qt 6.9.0 许可证**: Qt 自身提供多种许可选项。本项目明确选择并依据其 **GNU Lesser General Public License v3.0 (LGPL-3.0)** 来使用 Qt 6.9.0 的相关模块。你可以在 [Qt官网](https://www.qt.io/licensing/open-source-lgpl-obligations) 详细了解 LGPL-3.0 的义务，并从 [Qt官方下载页面](https://download.qt.io/official_releases/qt/6.9/6.9.0/single/) 获取 Qt 6.9.0 的完整源代码包 (例如 `qt-everywhere-src-6.9.0.tar.xz`)。

* **LGPLv3 对用户的权利与义务**:
    * **动态链接**: Simple-K Cloud Executor 的前端 (`front.exe`) **动态链接 (dynamically links)** 到 Qt 6.9.0 的共享库。这一点通过 `CMakeLists.txt` 中的 `target_link_libraries(front.exe PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)` 得以确认，CMake 在找到Qt包时默认会进行动态链接。
    * **替换Qt库的权利**: 根据 LGPLv3 第4节 (Section 4 "Conveying Modified Versions of the Library") 和第5节 (Section 5 "Conveying Non-Source Forms") 的规定，作为接收本软件的用户，你有权使用你自己编译和修改过的、与本项目兼容的Qt库版本来运行 `front.exe`。为了协助你行使此项权利，以下是详细的操作指引：

        **步骤 1：获取并解压 Qt 6.9.0 源代码**
        ```bash
        wget [https://download.qt.io/official_releases/qt/6.9/6.9.0/single/qt-everywhere-src-6.9.0.tar.xz](https://download.qt.io/official_releases/qt/6.9/6.9.0/single/qt-everywhere-src-6.9.0.tar.xz)
        tar -xf qt-everywhere-src-6.9.0.tar.xz
        cd qt-everywhere-src-6.9.0
        ```

        **步骤 2：配置和编译你自己的 Qt 6.9.0 共享库**
        你需要根据你的操作系统和编译器环境来配置Qt的编译。以下是一个在Linux环境下使用GCC编译的示例，目标是生成包含 `front.exe` 所需模块 (`Core`, `Gui`, `Widgets`, `Concurrent`) 的共享库，并安装到自定义路径 (例如 `/opt/custom-qt6`):
        ```bash
        # 进入解压后的Qt源码目录
        # 创建一个构建目录，Qt官方推荐在源码目录之外进行构建
        mkdir build-custom-qt
        cd build-custom-qt

        # 配置Qt编译。请根据你的实际需求和平台调整参数。
        # -prefix: 指定安装路径
        # -opensource -confirm-license: 同意开源许可
        # -release: 编译Release版本 (也可以选择 -debug-and-release)
        # -platform: 如果交叉编译或特定平台需要，可能需要指定
        # -qt-libpng, -qt-libjpeg, etc.: Qt可能依赖一些第三方库，可以指定使用系统版本或Qt自带版本
        # -nomake examples -nomake tests: 通常为了快速编译库本身，可以跳过示例和测试
        # -static (不要用这个，我们需要共享库)
        # -shared (通常是默认的)
        # 确保你选择的编译器与编译 Simple-K Executor 时使用的编译器ABI兼容
        ../configure -prefix /opt/custom-qt6 -opensource -confirm-license -release \
                     -platform linux-g++ \
                     -no-pch \
                     -nomake examples -nomake tests \
                     -skip qtwebengine -skip qtwayland # (如果不需要，可以跳过大型模块以加速编译)

        # 开始编译 (根据你的CPU核心数调整 -j 参数)
        cmake --build . --parallel $(nproc)

        # 安装到指定的 -prefix 路径
        cmake --install .
        ```
        **重要**: Qt的配置和编译过程可能比较复杂且耗时。请务必参考 [Qt官方文档](https://doc.qt.io/qt-6/configure-options.html) 和 [Building Qt Sources](https://doc.qt.io/qt-6/linux-building.html) (或其他对应平台的构建指南) 以获取最准确和最适合你环境的指令。

        **步骤 3：使用你的自定义Qt库运行 Simple-K Cloud Executor 前端**

        * **方法 A: 重新编译 `front.exe` 并链接到你的自定义Qt库**
            1.  在编译 Simple-K Cloud Executor 时，通过 `CMAKE_PREFIX_PATH` 环境变量或CMake参数告知构建系统你的自定义Qt库的安装路径：
                ```bash
                cd /path/to/Simple-K-Cloud-Executor
                # 如果使用了构建脚本 dmake.sh 或 make.sh, 修改其中的 CMAKE_PREFIX_PATH
                # 或者手动执行cmake:
                mkdir build-with-custom-qt
                cd build-with-custom-qt
                cmake .. -DCMAKE_PREFIX_PATH=/opt/custom-qt6 -DCMAKE_BUILD_TYPE=Release # 或 Debug
                cmake --build .
                ```
            2.  这样编译出的 `build-with-custom-qt/front.exe` 在支持 `RPATH`/`RUNPATH` 的系统上（如现代Linux发行版），其动态链接器通常会优先查找 `/opt/custom-qt6/lib` 下的Qt库。

        * **方法 B: 通过环境变量在运行时指定Qt库路径 (适用于已有的 `front.exe`)**
            如果你的 `front.exe` 是用系统默认Qt或其他版本的Qt编译的，你可以尝试在运行时通过环境变量临时指向你的自定义Qt库路径。
            * **Linux**:
                ```bash
                export LD_LIBRARY_PATH=/opt/custom-qt6/lib:$LD_LIBRARY_PATH
                /path/to/your/existing/build/front.exe
                ```
            * **Windows**: 将你编译生成的自定义Qt DLL文件 (例如 `Qt6Core.dll`, `Qt6Widgets.dll` 等) 从 `/opt/custom-qt6/bin` (或其他安装路径下的bin目录) 复制到与 `front.exe` 可执行文件相同的目录下，或者将该bin目录添加到系统的 `PATH` 环境变量中，并确保它在其他Qt版本的路径之前。
            * **macOS**:
                ```bash
                export DYLD_LIBRARY_PATH=/opt/custom-qt6/lib:$DYLD_LIBRARY_PATH
                /path/to/your/existing/build/front.exe
                ```
            **注意**: 运行时替换动态库需要确保ABI兼容性。如果 `front.exe` 编译时使用的Qt版本或编译器与你自定义编译的Qt库差异过大，可能会导致运行时错误。最可靠的方法通常是**方法 A**，即用你的自定义Qt库重新编译 `front.exe`。

**免责声明 (Disclaimer of Warranty):**

> 本软件按“原样”提供，**不作任何形式的保证**，无论是明示的还是暗示的，包括但不限于对适销性、特定用途适用性和非侵权性的保证。在任何情况下，作者或版权持有人均不对任何索赔、损害或其他责任承担任何责任，无论是在合同行为、侵权行为还是其他方面，由软件或软件的使用或其他交易引起或与之相关的。详细信息请参阅GPL-3.0许可证文本。

## 联络作者 (Contact) 💌

Author: **@kleedaisuki**
Email:  <kleedaisuki@outlook.com>

---

Happy Hacking! (づ｡◕‿‿◕｡)づ
