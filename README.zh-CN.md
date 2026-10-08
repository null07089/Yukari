# Yukari

Yukari 是一个按目标应用生效的 Zygisk 模块，用于隐藏自定义 ROM 的
ServiceManager 服务信号。固定匹配关键字为 `lineage`、`crdroid`、`aospa`、
`pixelexperience`、`omnirom`、`protonaosp`，并精确匹配 `profile`。
可选配置还会在目标进程内隐藏 `lineageos.platform` 资源包、
`org.lineageos.*` 系统 feature 以及 Lineage 受保护广播 Action（见下文配置）。

## 实现策略

目标进程在 `preAppSpecialize` 注册 `android.os.BinderProxy.transactNative`
的 JNI hook，并在 `postAppSpecialize` 清理一次 `ServiceManager.sCache`。该路径在 Parcel
层完成过滤，不改写 `libbinder.so` 的 PLT/GOT：

- 读取运行时 `IServiceManager.Stub.TRANSACTION_*`，兼容新 Lineage 插入
  `getService2`/`checkService2` 后的事务编号变化，不仅根据 SDK_INT 猜测；
- `getService`/`checkService` 及其 `*2` 版本仅对明确来自应用的匹配查询进行等长
  名称替换；先复制请求 Parcel，保留调用方原始内容与位置；
- 调用栈跳过 Binder/ServiceManager 和反射转发帧，以第一个实际调用方判定；
  框架/Lineage 调用、无法识别的调用和分类失败均保留真实 Binder，避免初始化 NPE；
- `listServices` 的 `String[]` 回复被过滤并以相同 UTF-16 长度写回；
- `getServiceDebugInfo` 的 `ServiceDebugInfo[]` 名称被过滤；
- 直接使用 `transactNative` 传入的 Java `Parcel` 对象，不接管 native 所有权；
  因此不会触碰 `mNativePtr` 或触发额外的 native 释放。

在极旧系统上，如果 JNI 方法签名不可用，则回退到 ioctl 过滤。回退路径仍然
使用原有的安全缓冲区交换，并通过匿名、RX-only 跳板作为 PLT 替换地址，避免
GOT 槽直接指向模块 `.text`。
该回退路径仍只过滤枚举/调试回复，不对直接 lookup 做调用方分类或重写。

`sCache` 仍仅在 specialization 时清理一次，不在事务回调中并发修改 Map。
核对的 Lineage `getCommonServicesLocked` 不向应用预填 `profile`，普通 lookup miss
也不会回填 `sCache`；厂商动态注入匹配缓存、纯 native libbinder 直查，以及通过
受信任框架/Lineage API 间接获取服务仍可能绕过当前边界。前缀保护是启动兼容策略，
不是强制安全边界；不能承诺任何检测方式均不可见。

### 追加 ROM 信号通道（可选）

除 ServiceManager 外，目标进程还可按需启用三条独立通道，全部只作用于目标进程、
只做等长改写、不修改系统镜像：

| 通道 | 钩子位置 | 效果 | 已知边界 |
| --- | --- | --- | --- |
| 资源包 | `AssetManager` native 名称/ID 查询 | `lineageos.platform`（资源包 id `0x3f`）按不存在处理 | 需要 Android 9+；包列表与 SDK 类仍可见 |
| 系统 feature | `IPackageManager.hasSystemFeature` 请求改写 + `Parcel.nativeReadString8/16` 返回值替换 | `hasSystemFeature` 返回 `false`，枚举结果为等长占位符 | 依赖 `org.lineageos.*` 决定自身集成的应用会降级 |
| 受保护广播 | 复制 `IActivityManager` 广播请求并等长替换 10 个 lineage Action | 发送不再抛 `SecurityException`，等同 AOSP 静默成功 | `PendingIntent` 代发路径不经过应用事务，不在覆盖内 |

**更新模块后必须重启设备**：zygote 常驻映射模块 `.so`，热替换正在映射的文件会让
新旧页混用并导致 zygote 崩溃。三条通道默认关闭，阈值和风险见下节配置说明。

## 可观察特征取舍

| 特征 | 旧实现 | 当前实现 |
| --- | --- | --- |
| `ioctl` GOT 槽指向模块代码 | 是 | JNI 路径：否；旧系统回退：指向匿名跳板 |
| PLT/GOT 被改写 | 是 | JNI 路径：否 |
| `rwxp` 映射 | 可能出现 | 跳板创建后立即 `mprotect` 为 `r-x` |
| C++ 全局析构/`atexit` | `std::string`/配置对象可能注册 | 进程状态改为平凡可析构对象；配置故意驻留 |
| `.symtab`/私有符号 | 可能存在 | CMake 版本脚本 + `--strip-unneeded`，仅保留 Zygisk 入口 |
| `/proc/self/maps` 中模块路径 | 可见 | JNI hook 仍需驻留回调代码，因此路径仍可能可见 |

模块路径匿名化或在保留回调的同时 `dlclose` 会使函数指针悬空，带来高崩溃
风险，因此没有强行执行。需要做到“maps 完全无模块路径”时，必须把完整回调
运行时（代码、只读数据、TLS 和异常处理）搬迁到独立 ELF/匿名映射后再卸载，
这超出了安全的跨 Android 版本实现范围。

## 配置

```json
{
  "enabled": true,
  "force_denylist_unmount": true,
  "hide_lineage_resources": false,
  "hide_lineage_features": false,
  "hide_lineage_broadcasts": false,
  "targets": ["com.example.app"]
}
```

仅列出的应用进程会启用过滤，系统进程和受保护包始终跳过。
如果目标应用依赖 Magisk 挂载的文件或资源，可将 `force_denylist_unmount` 设为
`false`；服务过滤仍然生效。

`hide_lineage_resources` 设为 `true` 时，还会在目标进程内隐藏
`lineageos.platform` 资源包：对资源包 id `0x3f`（以及 `defPackage` 为
`lineageos.platform`）的 AssetManager 名称/ID 查询返回“不存在”，效果等同于
非 LineageOS 设备。该实现不改写 PackageManager，也不修改系统镜像；真正使用
Lineage SDK 资源的目标应用会失去这些资源，而包列表、SDK 类和 `/system`
文件仍然可见。除确有该类探测需求的目标外建议保持关闭。该隐藏依赖 Android 9
（API 28）起的 AssetManager 方法签名；旧系统上开关会被跳过，资源视图保持原样。

`hide_lineage_features` 设为 `true` 时，还会在目标进程内隐藏这 8 个 LineageOS
系统 feature：`org.lineageos.livedisplay`、`org.lineageos.profiles`、
`org.lineageos.hardware`、`org.lineageos.globalactions`、`org.lineageos.trust`、
`org.lineageos.health`、`org.lineageos.android`、`org.lineageos.settings`。
`PackageManager.hasSystemFeature()` 对它们返回 `false`（该 API 本身返回
boolean，不存在 null）；`getSystemAvailableFeatures()` 中的名称会被替换为等长
下划线占位符，Parcel 布局保持不变。依赖这些 feature 决定自身 Lineage 集成的
目标应用应保持关闭。

`hide_lineage_broadcasts` 设为 `true` 时，会改写目标应用发往 `IActivityManager`
的广播请求中的 Lineage 受保护广播 Action（如
`lineageos.intent.action.REFRESH_PREFERENCE`、`lineageos.platform.intent.action.PROFILE_SELECTED`
等 10 个）。效果：在 LineageOS 上发送这些 Action 不再抛 `SecurityException`，
而是像 AOSP 一样静默成功（无接收者）；Action 在私有请求副本里被等长占位符替换，
应用自身的 Intent 不受影响。依赖这些广播探测 ROM 的场景应开启。

运行模块 action 可通过序号合并或替换 targets，`a` 为全选合并、`k` 保留、`q` 取消。
Magisk 管理器没有终端时，音量上键全选合并、音量下键进入逐包选择；超时保持原文件。
脚本验证已知配置字段；未知字段或不支持的 JSON 转义会中止写入并保留原配置。

## 分阶段验证

对应的实现也可以分阶段启用：

- 仅保留 `service_cache.cpp`，验证 `sCache` 清理；
- 启用 `BinderProxy.transactNative` JNI hook，验证列表/调试回复；
- 在不支持该 JNI 签名的旧系统上启用 `install_hooks()` ioctl 回退；
- 需要时打开 `hide_lineage_resources`，验证目标进程内的资源隐藏；
- 按需打开 `hide_lineage_features`、`hide_lineage_broadcasts`，验证 feature 与广播隐藏；
- 最后打开 CMake 的 strip/version-script 检查，确认发布 ELF 不含私有符号。

1. **构建检查**

   本地需要 JDK 17、Gradle 8.11.1 和 Android SDK/NDK。仓库的 `gradlew` 是调用
   `PATH` 中 Gradle 的入口；CI 使用 `setup-gradle` 安装固定版本。
   CI 的 SDK setup 显式只安装 `platform-tools`，避免 action 默认安装已下架的
   `tools` 包导致编译之前失败；SDK 36、NDK 和 CMake 仍由后续步骤固定安装。

   ```bash
   ./gradlew :module:assembleRelease
   bash scripts/package.sh
   ```

2. **ELF 符号检查**（设备或 CI 主机）

   ```bash
   unzip -p out/Yukari.zip zygisk/arm64-v8a.so >/tmp/yukari.so
   readelf -Ws /tmp/yukari.so
   # 预期：仅有 zygisk_module_entry 动态导出，不出现 hook_ioctl 等私有符号
   ```

3. **运行时 GOT 检查**

   在目标进程中读取 `libbinder.so` 的 `ioctl` 槽，并用 `dladdr` 检查归属。
   JNI 主路径不会修改该槽；旧系统回退路径的地址应落在匿名 `r-xp` 跳板映射。

4. **映射检查**

   ```bash
   adb shell 'cat /proc/$(pidof your.target)/maps | grep -E "yukari|rwxp"'
   ```

   不应有 `rwxp`；JNI 路径下仍可能看到模块的只读/可执行文件映射（见上表）。

5. **功能回归**

   在目标应用中调用 `getService("profile")`、`checkService`、
   `listServices` 和 `getServiceDebugInfo`，并覆盖新的 `getService2`/`checkService2`。
   批量枚举和调试信息中的匹配项应不可见；应用直接 lookup 匹配项应拿不到服务，
   框架启动和 Lineage 内部查询应仍获取真实 Binder。不要通过外部 `adb shell service list`
   判断过滤结果：它不在目标应用进程内。非匹配项、非目标应用均保持正常。
   同时验证请求 `dataSize/dataPosition` 和字节内容未改变、真实 Binder 异常仍正常传播。
   确认 logcat 的 `SM transactions` 日志与该 ROM 的实际事务号一致；启动阶段不得出现
   NPE 或 native 崩溃。

6. **资源隐藏验证**（`hide_lineage_resources: true`）

   logcat 中目标进程应出现 `AssetManager resource hook installed`。在目标应用内
   `getResourcesForApplication("lineageos.platform")` 仍会成功（模块不隐藏包本身），
   但 `getIdentifier("config_enableLiveDisplay", "bool", "lineageos.platform")` 返回 0，
   对应 `getBoolean`/`getInteger` 抛出 `NotFoundException`；包列表和 SDK 类不受影响。
   同一设备上的非目标对照应用仍应读到 `true`/`6500`，确认隐藏只在目标进程生效。
   同时回归目标应用的核心功能，确认没有资源异常或崩溃。

7. **Feature 隐藏验证**（`hide_lineage_features: true`）

   目标应用内 `getPackageManager().hasSystemFeature("org.lineageos.livedisplay")`
   应返回 `false`，`getSystemAvailableFeatures()` 不应再出现真实的
   `org.lineageos.*` 名称；非目标对照应用仍应返回 `true` 并列出这些 feature。

8. **广播隐藏验证**（`hide_lineage_broadcasts: true`）

   目标应用内 `sendBroadcast(new Intent("lineageos.intent.action.REFRESH_PREFERENCE"))`
   不应抛 `SecurityException`，logcat 出现 `scrubbed N lineage broadcast action(s)`；
   非目标对照应用发送同一 Action 仍应被系统拒绝。
