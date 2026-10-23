# Liteview

Liteview 是一款面向 Windows 的轻量、便携浏览器，提供日常浏览所需的地址栏、多标签页、多窗口、网页缩放和历史记录。

界面使用原生 C++ 和 Win32 编写，网页由电脑上的 Microsoft Edge WebView2 Runtime 加载。程序不捆绑浏览器内核，因此 EXE 本体可以保持很小；解压 Release 包后即可运行，浏览数据保存在程序旁边。

## 主要功能

- **网址与搜索共用地址栏**：输入网址直接访问，输入关键词使用必应（Bing）搜索，支持中文。
- **基础导航**：前进、后退、刷新、停止加载和返回内置主页，支持鼠标侧键。
- **多标签页**：标签栏位于窗口顶部，支持新建、切换、关闭；各标签独立保留页面和前进、后退记录。
- **多窗口**：通过菜单或快捷键新建窗口，同一次运行中的窗口共享浏览数据。
- **网页缩放**：通过菜单或快捷键在 25%～500% 之间调整，可一键恢复到 100%。
- **全屏浏览**：按 F11 切换全屏，按 Esc 退出全屏。
- **历史记录**：查看最近访问的页面，按标题或网址筛选，点击记录重新访问。
- **本地网页**：在地址栏输入本地 HTML 文件的完整路径或 `file://` 地址即可打开。
- **便携数据**：缓存、Cookie 和网站存储等保存在 EXE 旁的 `Data` 文件夹中。

## 运行要求

- 当前以 **Windows 11** 为开发和验证环境，其他 Windows 版本尚未完整验证。
- 电脑需要安装 **Microsoft Edge WebView2 Evergreen Runtime**。如果启动时提示缺少运行时，请从微软官方渠道安装。
- 程序所在文件夹需要有写入权限，以便创建和使用 `Data`。

发布版提供 x64 和 x86 两种构建配置。Windows 11 电脑通常选用 x64 即可；x86 指 32 位版本。当前项目未提供 ARM64 原生构建配置。

Release 版静态链接 WebView2 加载器和 MSVC 运行库，无需额外携带 `WebView2Loader.dll` 或安装 VC++ 运行库，但仍然需要 WebView2 Runtime。

## 开始使用

1. 将便携版压缩包解压到一个有写入权限的文件夹。
2. 双击 `Liteview.exe` 启动。
3. 在地址栏输入网址或搜索内容，按 Enter，或点击地址栏右端的箭头。

程序首次运行后，目录大致如下：

```text
Liteview/
├─ Liteview.exe         浏览器程序
├─ README.txt           便携版使用说明
├─ WebView2-LICENSE.txt  WebView2 加载器许可声明
├─ LICENSE              Liteview 的 MIT 许可证，分发时应附带
└─ Data/                运行后生成的浏览数据
```

### 地址栏

| 输入示例 | 行为 |
| --- | --- |
| `example.com` | 自动补全为 HTTPS 网址并访问 |
| `https://example.com` | 按输入的网址访问 |
| `天气预报` | 使用必应搜索 |
| `localhost:8080` | 默认使用 HTTP 访问本机服务 |
| `C:\Web\index.html` | 打开本地 HTML 文件 |

程序根据输入内容判断是网址还是搜索词。需要明确使用某种协议时，请输入完整地址，例如 `http://192.168.1.10`。

### 标签页与窗口

- 点击最后一个标签右侧的 **+** 新建标签，灰色背景表示当前标签。
- 点击标签上的 **×**，或用鼠标中键点击标签，可以关闭它。
- 标签较多时，可以使用标签栏的滚动箭头查看。
- 网页请求打开新窗口的链接，会在新标签中打开。
- 拖动顶部空白区域可以移动窗口，双击该区域可以最大化或还原窗口。
- 关闭最后一个标签会关闭所在窗口；其他窗口仍可继续使用。

### 三点菜单

地址栏右侧的 **…** 菜单提供新建标签页、新建窗口、缩放、全屏和历史记录等操作。

点击缩放百分比可以恢复到 100%。右上角的关闭按钮只关闭当前窗口；菜单最下方的“关闭 Liteview”会关闭本次运行创建的全部窗口。

## 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Ctrl+L` / `Ctrl+K` | 选中地址栏内容 |
| `Enter`（地址栏中） | 访问网址或搜索 |
| `Alt+←` / `Alt+→` | 后退 / 前进 |
| `F5` / `Ctrl+R` | 刷新；页面正在加载时改为停止 |
| `Esc` | 停止加载；全屏时退出全屏；菜单展开时收起菜单 |
| `Alt+Home` | 打开内置主页 |
| `Ctrl+T` | 新建标签页 |
| `Ctrl+W` / `Ctrl+F4` | 关闭当前标签页 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 切换到下一个 / 上一个标签页 |
| `Ctrl+1`～`Ctrl+8` | 切换到对应位置的标签页 |
| `Ctrl+9` | 切换到最后一个标签页 |
| `Ctrl+N` | 新建窗口 |
| `Ctrl++` / `Ctrl+-` | 放大 / 缩小网页 |
| `Ctrl+0` | 恢复 100% 缩放 |
| `F11` | 进入 / 退出全屏 |
| `Ctrl+H` | 打开历史记录 |
| `Alt+F` / `F10` | 展开三点菜单 |

## 数据保存与迁移

程序使用 EXE 所在目录下的 `Data` 保存浏览数据，不会因启动时的工作目录不同而改变位置。

- **备份或迁移**：关闭全部 Liteview 窗口，再复制整个程序文件夹。
- **重置浏览数据**：关闭全部窗口后删除 `Data`，下次启动会创建新的数据目录。原来的历史记录、登录状态和网站数据会丢失。
- **升级程序**：关闭全部窗口后替换程序文件，保留 `Data` 即可继续使用原有浏览数据。

历史记录保存于 `Data/history.tsv`，记录成功打开的 HTTP、HTTPS 页面和本地文件，不记录内置主页和历史页。文件超过约 1 MiB 后会自动缩减，保留较近的记录；历史页最多展示最近 500 条，并受页面大小上限限制。筛选作用于当前展示的记录。

程序不创建安装项、卸载项、系统快捷方式或自身的注册表配置。WebView2 和 Windows 仍可能留下正常运行记录，便携版并不表示无痕浏览。网站登录凭据可能受系统加密保护，复制数据到另一台电脑后不保证保留登录状态。

用户下载的文件由下载对话框选择保存位置，不一定在程序目录内。

## 从源码构建

### 开发环境

当前项目使用：

- Visual Studio 2026，安装“使用 C++ 的桌面开发”工作负载。
- MSVC **v145** 工具集与 Windows SDK。
- C++20。
- Microsoft.Web.WebView2 SDK **1.0.4258.31**。

**WebView2 SDK 用于编译，WebView2 Runtime 用于运行。** 两者用途不同，安装 Runtime 不会自动补齐项目需要的 SDK。

### 使用 Visual Studio

1. 准备 WebView2 SDK，确认以下文件存在：

   ```text
   packages/Microsoft.Web.WebView2.1.0.4258.31/build/native/include/WebView2.h
   ```

   `packages` 未纳入 Git。首次获取源码后，可以在项目根目录执行以下命令，自动下载 SDK，并完成一次 x64 Release 构建：

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\build.ps1
   ```

   如果希望手动准备 SDK，可下载上述版本的 `Microsoft.Web.WebView2` NuGet 包，将其作为 ZIP 解压到 `packages/Microsoft.Web.WebView2.1.0.4258.31/`，避免额外嵌套一层目录。除头文件外，也应保留包内的静态库和许可证文件。

2. 打开 `Liteview.slnx`。
3. 选择 **Release / x64**。构建 32 位版本时选择 **Release / x86**，项目内部名称为 `Win32`。
4. 执行“生成解决方案”。

| 构建配置 | 程序输出位置 |
| --- | --- |
| Release / x64 | `dist/x64/Liteview.exe` |
| Release / x86（Win32） | `dist/Win32/Liteview.exe` |
| Debug / x64 | `build/x64/Debug/bin/Liteview.exe` |
| Debug / x86（Win32） | `build/Win32/Debug/bin/Liteview.exe` |

生成后，使用说明和 WebView2 加载器许可证会一起复制到输出目录。VS 构建不会自动生成或更新 ZIP 压缩包。

日常调试可以选择 Debug；对外发布应选择 Release，以使用体积优化和发布版配置。

### 使用构建脚本（可选）

脚本调用同一份 VS 项目配置，不需要另装一套编译器。在项目根目录执行：

```powershell
# x64 Release：默认配置
powershell -ExecutionPolicy Bypass -File .\build.ps1

# x86 Release
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Platform Win32

# x64 Debug
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug
```

Release 构建会生成 `dist/Liteview-portable-x64.zip` 或 `dist/Liteview-portable-x86.zip`。当前脚本仅打包 EXE、使用说明和 WebView2 加载器许可证；对外分发前还应将根目录的 `LICENSE` 加入发布包。不要将自己的 `Data` 一起打包。

## 项目结构

| 文件或目录 | 用途 |
| --- | --- |
| `main.cpp` | 程序入口、窗口界面、标签页、导航、菜单和历史记录 |
| `Liteview.slnx` | Visual Studio 解决方案入口 |
| `Liteview.vcxproj` | 编译、链接、依赖路径和输出配置 |
| `Liteview.vcxproj.filters` | VS 中的文件分组 |
| `app.manifest` | 权限、系统兼容性和高 DPI 声明 |
| `app.rc` / `resource.h` | Windows 资源脚本和资源编号 |
| `assets/` | 原始 SVG、Windows 图标和内置主页图标数据 |
| `build.ps1` | 下载 SDK、编译及打包 |
| `PORTABLE.txt` | 分发时使用的简明说明 |
| `LICENSE` | 项目许可证 |
| `AGENTS.md` | 面向编程助手的项目开发约定 |

图标已经预先生成为 `assets/app.ico` 和 `assets/home-icon.inc`，普通构建无需图标转换工具。修改原始 `assets/app.svg` 后，需要同步更新这两个生成文件；现有构建流程不会自动转换图标。

## 当前限制

项目优先使用 Win32 和现有 WebView2 能力，通过 Release 体积优化、静态加载器和静态运行库减少分发文件，不使用压缩壳。

目前尚未实现：

- 退出后恢复上次打开的标签页。
- 收藏夹管理。
- 无痕窗口。
- 浏览器扩展管理。
- 跨设备同步。
- 在界面中更换默认搜索引擎。

程序本体小不代表网页占用的内存和存储也小。WebView2 会根据网页内容启动多个进程，缓存也会随使用增长。不同显示器之间的 DPI 切换、所有网站的登录及弹窗流程尚未全面验证。

## 许可证

Liteview 自有代码采用 [MIT 许可证](LICENSE)，版权信息见许可证文件。

WebView2 等第三方组件遵循各自的许可条款。分发程序时，请保留 Liteview 的 `LICENSE`、便携版说明和 `WebView2-LICENSE.txt`。
