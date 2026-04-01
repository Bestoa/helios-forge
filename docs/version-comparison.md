# Helios Forge 5 个版本实现对比

本文对仓库中的 5 个版本进行实现细节对比，并额外核查物理参数是否存在差异。

## 版本列表

1. C + OpenGL: [solar_system_modern.c](solar_system_modern.c)
2. C + Vulkan: [solar_system_vulkan.c](solar_system_vulkan.c)
3. Objective-C + Metal: [solar_system_metal.m](solar_system_metal.m)
4. Python + Pygame + PyOpenGL: [solar_system_modern.py](solar_system_modern.py)
5. TypeScript + Three.js: [web/src/main.ts](web/src/main.ts), [web/src/simulation.ts](web/src/simulation.ts), [web/src/data.ts](web/src/data.ts)

## 总体结论

- 5 个版本的核心场景语义一致。
- 轨道数学、行星参数、交互语义大体一致。
- 主要差异集中在渲染后端、资源绑定方式、宿主平台事件模型、UI 展示方式、代码组织方式。
- 最可能的母版是 C + OpenGL 版本，Python、Metal、Vulkan 都能看出较强的同源痕迹。
- Web 版不是简单 API 翻译，而是保留仿真语义后进行了前端工程化重构。

## 逐项对照表

| 维度 | C + OpenGL | C + Vulkan | Objective-C + Metal | Python + PyOpenGL | Web + Three.js |
| --- | --- | --- | --- | --- | --- |
| 入口与宿主 | GLFW 窗口循环，[solar_system_modern.c:1179](solar_system_modern.c:1179) | GLFW 只负责窗口，渲染走 Vulkan，[solar_system_vulkan.c:2526](solar_system_vulkan.c:2526) | Cocoa + `CAMetalLayer`，[solar_system_metal.m:1552](solar_system_metal.m:1552) | Pygame 窗口与事件，[solar_system_modern.py:840](solar_system_modern.py:840) | 浏览器 DOM + `WebGLRenderer`，[web/src/main.ts:66](web/src/main.ts:66) |
| 场景数据组织 | 纯 C struct，[solar_system_modern.c:87](solar_system_modern.c:87) | 场景 struct 外包一层 `VulkanApp`，[solar_system_vulkan.c:156](solar_system_vulkan.c:156) | `MetalApp` 同时管理平台层和场景层，[solar_system_metal.m:178](solar_system_metal.m:178) | 类式封装，[solar_system_modern.py:104](solar_system_modern.py:104) | 仿真和渲染拆文件，[web/src/simulation.ts:12](web/src/simulation.ts:12) |
| 行星参数来源 | 初始化函数硬编码，[solar_system_modern.c:1020](solar_system_modern.c:1020) | 初始化函数硬编码，[solar_system_vulkan.c:634](solar_system_vulkan.c:634) | 初始化函数硬编码，[solar_system_metal.m:896](solar_system_metal.m:896) | `SolarSystem.__init__` 中硬编码，[solar_system_modern.py:570](solar_system_modern.py:570) | 独立数据文件，[web/src/data.ts:32](web/src/data.ts:32) |
| 轨道数学 | 手写 Kepler 解算与轨道基向量，[solar_system_modern.c:763](solar_system_modern.c:763) | 同算法，Vulkan 化，[solar_system_vulkan.c:450](solar_system_vulkan.c:450) | 同算法，Metal 化，[solar_system_metal.m:672](solar_system_metal.m:672) | 同算法，Python 化，[solar_system_modern.py:240](solar_system_modern.py:240) | 同算法，但抽到独立仿真层，[web/src/simulation.ts:38](web/src/simulation.ts:38) |
| 几何生成 | 球、环、轨道、星空都手写生成，[solar_system_modern.c:607](solar_system_modern.c:607) | 同样自己生成，再上传 Vulkan buffer，[solar_system_vulkan.c:487](solar_system_vulkan.c:487) | 同样自己生成，再上传 `MTLBuffer`，[solar_system_metal.m:537](solar_system_metal.m:537) | 同样自己生成，但用 Python/numpy 管理，[solar_system_modern.py:164](solar_system_modern.py:164) | 球体与环多交给 Three.js 标准几何，轨道点和星空数据自己算，[web/src/main.ts:279](web/src/main.ts:279) |
| Mesh 管理 | VAO/VBO/EBO 手动维护，[solar_system_modern.c:75](solar_system_modern.c:75) | `Geometry` 自带 buffer 和 memory，[solar_system_vulkan.c:95](solar_system_vulkan.c:95) | `Mesh` 持有 `MTLBuffer`，[solar_system_metal.m:49](solar_system_metal.m:49) | `Mesh` 类封装 OpenGL buffer，[solar_system_modern.py:351](solar_system_modern.py:351) | Three.js `Mesh/Group/Points/LineLoop` 接管对象管理，[web/src/main.ts:282](web/src/main.ts:282) |
| Shader 管理 | 从文件编译 GLSL，[solar_system_modern.c:387](solar_system_modern.c:387) | GLSL 编译成 SPIR-V，再建 shader module/pipeline，[solar_system_vulkan.c:1404](solar_system_vulkan.c:1404) | 运行时编译 `.metal` 源文件，[solar_system_metal.m:1150](solar_system_metal.m:1150) | Python 包装 GLSL 编译，[solar_system_modern.py:104](solar_system_modern.py:104) | shader 字符串直接写在 TS 内，[web/src/main.ts:125](web/src/main.ts:125) |
| Uniform / 常量传递 | 全部 `glUniform*`，[solar_system_modern.c:443](solar_system_modern.c:443) | 全局 UBO + Push Constants，[solar_system_vulkan.c:116](solar_system_vulkan.c:116) | `setVertexBytes/setFragmentBytes` 传结构体，[solar_system_metal.m:1338](solar_system_metal.m:1338) | Python 逐个设 uniform，[solar_system_modern.py:133](solar_system_modern.py:133) | `ShaderMaterial.uniforms`，[web/src/main.ts:235](web/src/main.ts:235) |
| 纹理管理 | 每次 draw 前切纹理槽并绑定纹理，[solar_system_modern.c:1399](solar_system_modern.c:1399) | 所有纹理预先进入 descriptor array，draw 时传 `texture_index`，[solar_system_vulkan.c:2139](solar_system_vulkan.c:2139) | 每次 draw 绑定 texture + sampler，[solar_system_metal.m:1491](solar_system_metal.m:1491) | 与 OpenGL C 版同构，[solar_system_modern.py:1007](solar_system_modern.py:1007) | `TextureLoader` 加载后挂到材质 uniform，[web/src/main.ts:283](web/src/main.ts:283) |
| 每帧执行模型 | 事件轮询后立即 draw，[solar_system_modern.c:1265](solar_system_modern.c:1265) | 更新 UBO，录命令缓冲，submit，再 present，[solar_system_vulkan.c:2461](solar_system_vulkan.c:2461) | 编码 command buffer 后 present drawable，[solar_system_metal.m:1356](solar_system_metal.m:1356) | Pygame loop 内直接 draw，[solar_system_modern.py:897](solar_system_modern.py:897) | `requestAnimationFrame` 驱动，[web/src/main.ts:581](web/src/main.ts:581) |
| 星空实现 | 自定义点云 + 点大小 shader，[solar_system_modern.c:817](solar_system_modern.c:817) | 自定义点云 + 独立 star pipeline，[solar_system_vulkan.c:614](solar_system_vulkan.c:614) | 自定义点云 + star pipeline，[solar_system_metal.m:721](solar_system_metal.m:721) | 自定义点云 + brightness attribute，[solar_system_modern.py:632](solar_system_modern.py:632) | 点云数据自己生成，显示用 `PointsMaterial + CanvasTexture`，[web/src/main.ts:317](web/src/main.ts:317) |
| 轨道线实现 | 自定义 `line strip`，[solar_system_modern.c:797](solar_system_modern.c:797) | 自定义 geometry + orbit pipeline，[solar_system_vulkan.c:593](solar_system_vulkan.c:593) | 自定义 `line strip`，[solar_system_metal.m:706](solar_system_metal.m:706) | `GL_LINE_STRIP`，[solar_system_modern.py:640](solar_system_modern.py:640) | `BufferGeometry + LineLoop`，[web/src/main.ts:312](web/src/main.ts:312) |
| 土星环实现 | 自定义 ring mesh，手动开 blend，[solar_system_modern.c:1403](solar_system_modern.c:1403) | ring pipeline + texture index 9，[solar_system_vulkan.c:2388](solar_system_vulkan.c:2388) | ring pipeline + clamp sampler，[solar_system_metal.m:1496](solar_system_metal.m:1496) | 与 OpenGL C 版几乎同构，[solar_system_modern.py:1011](solar_system_modern.py:1011) | `RingGeometry` + 自写 ring shader，[web/src/main.ts:293](web/src/main.ts:293) |
| 相机交互 | 鼠标拖拽、滚轮、WASD、锁定，[solar_system_modern.c:905](solar_system_modern.c:905) | 与 OpenGL C 基本一致，[solar_system_vulkan.c:770](solar_system_vulkan.c:770) | 事件先落到 `InputState`，再统一处理，[solar_system_metal.m:1283](solar_system_metal.m:1283) | Pygame event 驱动，[solar_system_modern.py:470](solar_system_modern.py:470) | 浏览器 pointer/wheel/keydown/resize，[web/src/main.ts:455](web/src/main.ts:455) |
| HUD / 状态展示 | 窗口标题栏，[solar_system_modern.c:1108](solar_system_modern.c:1108) | 窗口标题栏，[solar_system_vulkan.c:823](solar_system_vulkan.c:823) | 窗口标题栏，[solar_system_metal.m:1268](solar_system_metal.m:1268) | 独立 2D HUD 纹理层，[solar_system_modern.py:685](solar_system_modern.py:685) | DOM HUD，[web/src/main.ts:442](web/src/main.ts:442) |
| 截帧能力 | `glReadPixels` 导出 PNG，[solar_system_modern.c:579](solar_system_modern.c:579) | 当前主渲染流程里未见同等 CLI 截帧实现 | 读 drawable 纹理后导出 PNG，[solar_system_metal.m:502](solar_system_metal.m:502) | framebuffer 读回后交给 pygame 保存，[solar_system_modern.py:343](solar_system_modern.py:343) | 当前前端版没有内建 CLI 截帧 |

## 更细的实现差异

### 1. 仿真层

- OpenGL C、Vulkan、Metal、Python 四版都把数据定义、仿真更新、渲染调用放在单文件入口里。
- Web 版把仿真层拆到了 [web/src/simulation.ts](web/src/simulation.ts)，渲染和输入留在 [web/src/main.ts](web/src/main.ts)。
- 从架构上看，Web 版职责边界最清楚。

### 2. 几何层

- OpenGL C、Vulkan、Metal、Python 四版都自己生成 sphere、ring、orbit、star 顶点，因此视觉骨架天然一致。
- Web 版只有轨道点和星空分布保留手写，球体与环转交 Three.js 几何类管理，维护成本更低，但底层教学价值更弱。

### 3. 资源绑定层

- OpenGL C / Python 采用传统即时式绑定：谁要画，谁就在当下绑定纹理和 uniform。
- Vulkan 采用显式资源模型：全局数据进 UBO，物体差异进 Push Constants，纹理走 descriptor array。
- Metal 介于两者之间：pipeline 显式，但单次 draw 的数据传递比 Vulkan 轻得多。

### 4. 平台事件层

- GLFW 双版本最像，差别主要在图形 API。
- Metal 版额外实现了 Cocoa 事件桥接。
- Python 版依赖 Pygame 事件系统。
- Web 版依赖浏览器 pointer、wheel、keyboard、resize、blur 事件。

### 5. UI 展示层

- OpenGL C、Vulkan、Metal 三个桌面低层版都用窗口标题栏显示状态。
- Python 版额外做了真正的屏幕 HUD。
- Web 版则完全使用 DOM HUD，与渲染管线松耦合。

## 它们之间的继承关系

### 明显同源的部分

- OpenGL C、Metal、Python 三版高度同源。
- 共同的函数切分非常明显：
  - `create_sphere_mesh`
  - `create_ring_mesh`
  - `create_orbit_path`
  - `generate_stars`
  - `planet_init`
  - `planet_position`
  - `planet_model_matrix`
  - `solar_system_init`
  - `solar_system_update`
  - `camera_init`
  - `camera_move`
  - `camera_look_at_target` 或其等价形式
- 连常量也保持一致：
  - 球体分段 `40 x 28`
  - 星数 `1800`
  - 轨道线段数 `192`
  - 公转时间倍率 `60`
  - 自转时间倍率 `8`

### 更像“API 翻译”的版本

- Vulkan 版更像把已有场景逻辑搬进 Vulkan 资源模型。
- Metal 版更像把 OpenGL 风格的场景逻辑平移到 Metal。
- Python 版更像把 OpenGL C 版翻成更高层、可实验的 Python 实现。

### 真正有架构设计变化的版本

- Web 版变化最大。
- 它不是逐函数翻译，而是把程序重新拆成：
  - 数据配置层：[web/src/data.ts](web/src/data.ts)
  - 仿真层：[web/src/simulation.ts](web/src/simulation.ts)
  - 渲染与交互层：[web/src/main.ts](web/src/main.ts)
- 它还把底层绘制职责部分交给 Three.js，这属于设计形态变化，不只是 API 替换。

## 物理参数核查

### 核查范围

这里的“物理参数”主要检查：

- 轨道半径
- 轨道离心率
- 轨道倾角
- 升交点黄经
- 近地点幅角
- 行星显示半径
- 公转周期
- 初始相位
- 自转轴倾角
- 自转周期
- 土星环参数
- 太阳半径与自转周期

对应位置：

- OpenGL C: [solar_system_modern.c:1020](solar_system_modern.c:1020)
- Vulkan: [solar_system_vulkan.c:634](solar_system_vulkan.c:634)
- Metal: [solar_system_metal.m:896](solar_system_metal.m:896)
- Python: [solar_system_modern.py:570](solar_system_modern.py:570)
- Web: [web/src/data.ts:32](web/src/data.ts:32)

### 核查结果

- 核心物理参数在 5 个版本中一致。
- 8 颗行星的轨道参数、自转参数、尺寸、初始相位一致。
- 土星环内外半径在有环版本中一致，都是 `1.0` 与 `1.6`。
- 太阳半径和自转周期一致，都是 `1.5` 与 `24.47`。

### 唯一可见差异

- Vulkan 对没有环的行星，把 `ringColor` 设成了 `vec3(0, 0, 0)`，见 [solar_system_vulkan.c:634](solar_system_vulkan.c:634)。
- OpenGL C、Metal、Python、Web 对没有环的行星，把 `ringColor` 设成了白色默认值 `[1,1,1]` 或 `vec3(1,1,1)`，见 [solar_system_modern.c:1020](solar_system_modern.c:1020), [solar_system_metal.m:896](solar_system_metal.m:896), [solar_system_modern.py:570](solar_system_modern.py:570), [web/src/data.ts:32](web/src/data.ts:32)。
- 但这个差异不影响实际结果，因为这些行星的 `hasRing` 全部为 `false`，该值不会参与绘制。

### 需要注意但不算物理参数差异的点

- Vulkan 版把贴图路径直接写成 `"assets/textures/xxx.jpg"`，其他几个版本更多是存裸文件名或资源 URL。这属于资源定位方式差异，不是物理参数差异。
- 初始自转角和太阳初始角虽然都使用固定种子思路，但随机数实现不完全同源：
  - OpenGL C 和 Metal 用 `srand(42)` 后的 C 随机数。
  - Python 用 `random.seed(42)`。
  - Web 用 `mulberry32(42)`。
- 因此不同版本启动时某些“初始自转角”可能不是同一个具体数值，但这属于初始化随机实现差异，不属于行星轨道或物理常量差异。

## 最简结论

- 如果你关心图形 API 差异，重点比较 OpenGL / Vulkan / Metal。
- 如果你关心代码组织差异，重点比较 OpenGL C / Python / Web。
- 如果你关心物理参数是否一致，答案是：核心参数一致，只有 Vulkan 的无效 `ringColor` 默认值不同，不影响结果。
