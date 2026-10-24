# SWEEP-063 — Ghidra 工具化加速列车（«ابزار های مثل Ghidra و پلاگین ها» 指令落地）

日期：2026-10-24 ｜ 基线：origin/main = 97503e7（SWEEP-062 Train 4，fetch 核查 IN SYNC）
用户指令：用 Ghidra 及其插件把数小时的逆向分析压缩到几分钟，并实测真实效果。

## 1. 工具链安装（第一规则：GitHub 直查最新版）

| 组件 | 版本 | 来源 | 结果 |
|------|------|------|------|
| Ghidra | 12.2 (ghidra_12.2_PUBLIC_20261016.zip, 569MB) | NSA GitHub releases | /home/z/my-project/tools/ghidra_12.2_PUBLIC |
| JDK | Temurin 25.0.5+8（Ghidra 12.2 要求 java.min=25；系统 JDK 21 被拒） | Adoptium API | OK |
| PyGhidra | 3.2.0 + JPype 1.5.2 (cp312 wheel，Ghidra 捆绑) | Ghidra 自带 pypkg/dist | OK |
| 插件形态 | 自写导出脚本（scripts/sweep063_ghidra_export.py + sweep063_driver.py） | 本仓库 | 已提交 |

踩坑记录（可复用）：launch.properties 需写 JAVA_HOME_OVERRIDE=/…jdk-25.0.5+8；
headless 的 analyzeHeadless 不带 PyGhidra 运行时（«Python is not available»）→ 必须走
`pyghidra` Python API 驱动（scripts/sweep063_driver.py），CLI 包装器与外壳 stdin 冲突。

## 2. 实测效果（«ببینی واقعا چقدر تاثیر داشته یا نه»）— 结论：有效，量化如下

| 目标 | 大小 | 首次全量分析 | 后续复用 | 导出 |
|------|------|--------------|----------|------|
| winmine.exe | ~120KB | ~4s | <1s | 170 函数 / 110 导入 / 8 图标+11 位图资源 |
| sol.exe | ~200KB | ~4s | <1s | 136 函数 / 135 导入 |
| cards.dll | ~200KB | ~3s | <1s | 16 函数 |
| USBDeview.exe (32位) | ~100KB | ~2s | <1s | 551 函数 / 277 导入 |
| procexp64.exe | 5.2MB | **129s** | <1s | **2939 函数** / 767 导入 |

五目标六类导出（函数/导入/导出/入口+TLS/资源/字符串）+ 定点反编译，总耗时 **~150s**。
对照：SW-1292 此前用探针+手工反汇编跨多个批次（SWEEP-060..062）仍未收窄的绘制链，
本轮 Ghidra 反编译**一次读出**。效果 = 真实且巨大。

## 3. SW-1292 破案（sol WM_PAINT 链 — 本轮最大收获）

反编译四函数（evidence/sweep063/ghidra/sol/decomp/）：

1. `FUN_100007cf0`（fn[0] dispatch）：cmd=7 **落 default → FUN_1000047a0**（跳表推测被证实）
2. `FUN_1000047a0`（common handler）：`case 7: return FUN_100003f10(game, &ps)` — 链路直接，无隐藏 guard
3. `FUN_100003f10`（paint worker）：
   ```c
   uVar1 = FUN_100001690(*param_2, 0, 0);      // *param_2 = ps.hdc → 立即 SetPaintDC
   if (game->0x30 != 0 && 0 < game->0x70)      // 与探针实测完全吻合（13 堆）
       for each pile: (**(code**)(*pile + 8))(pile);   // 虚调用 vfn[1] 画每堆牌
   FUN_100001690(uVar1, 0, 0);                 // 还原 DC
   ```
4. `FUN_100001690`（f_1690 真身）：
   ```c
   uVar1 = DAT_10000c2a0;   DAT_10000c2ac = p2;  DAT_10000c2b0 = p3;
   DAT_10000c2a0 = p1;      return uVar1;        // 纯赋值，无任何分支/guard
   ```

**判决**：FUN_100003f10 第一条语句必然写 [c2a0]，且其函数体无分支。运行时实测
[c2a0] 保持 NULL + paint 内零 guest API 调用 + 无 fault ⇒ **控制权从未真正落到
FUN_100003f10 的第一条指令**。断点边界从「链中间某处」收窄为
**「0x100001d8d 的间接调用 `call *(%r11)` 的控制传递本身」**——引擎对该间接调用的
目标计算或块缓存 fast-path 静默空转返回。KILL 掉的假设：case-7 有条件短路（无）；
f_1690 有 guard（无）；paint_lock/13-pile gate（实测全通过）。
下一车厢 SW-1292b：guest-PC 采样核对 `call *(%r11)` 计算目标是否 == 0x100003f10。

## 4. SW-1204 证据（procexp64 序号导入 — Ghidra 数秒枚举）

Ghidra 导入表：SHLWAPI.Ordinal_176；WS2_32.Ordinal_{56,9,14,115,51,8,15}；
COMCTL32.Ordinal_{413,410,17,345}；ACLUI.Ordinal_1；
OLEAUT32.Ordinal_{6,2,277,4,25,16,24,23,19,20,12,9,8,7,150}。
与既有解码交叉验证：OLEAUT32.16=SafeArrayDestroy ✓、.277=VarUI4FromStr ✓（SW-1223/1225 吻合）。
待命名（下一车厢 SW-1204b，可配 Ghidra FID/BSim）：SHLWAPI.176、COMCTL32.413/410/17/345、
OLEAUT32.{2,6,7,8,9,12,19,20,23,24,25,150}、WS2_32.{8,9,14,15,51,56,115}、ACLUI.1。

## 5. SW-1293 检查单（NirSoft 32 位 PE32 预加载崩溃）

USBDeview 入口反编译给出 CRT 自检清单：GetModuleHandle(0) 读 MZ/PE 签名 →
OptionalHeader.Magic==0x10b → NumberOfRvaAndSizes>=14 → __SEH_epilog（SEH 框架）。
miniwin PE32 子系统预加载必须满足以上四点。1 个入口点、无 TLS 回调目录使用。

## 6. 扫雷资源审计（图形保真复核）

Ghidra .rsrc 枚举：8×IconResource + 1×GroupIcon + **11×BitmapResource**
（Rsrc_Bitmap_19a_409=2152B、19b=1072B、1a4=2312B …）——炸弹/数字/LED 位图**全部
在二进制资源段中完好**，佐证 SWEEP-062 Train 4 已用 DIB 调色板链路完成交互保真
（«بمب ها و سایر موارد کامل نیست» 已关闭）。剩余资源位图可按需逐张导出对照。

## 7. 引擎健康（本批无引擎代码改动）

winmine 交互运行 rc=0、82 帧（= SWEEP-061 基线）；截图
evidence/sweep063/shots_png/winmine_health_sweep063.png。

## 8. 新登记车厢

- **SW-1292b**：sol `call *(%r11)` @0x100001d8d 控制传递探针（目标地址采样 + 块缓存 fast-path 审计）
- **SW-1204b**：procexp64 序号导入命名（SHLWAPI.176 / COMCTL32.413/410/17/345 / OLEAUT32 尾部 / WS2_32 / ACLUI.1），候选工具 = Ghidra FID + BSim
- **SW-1293b**：PE32 预加载 CRT 自检清单落地（MZ/PE、0x10b、RvaAndSizes≥14、SEH）

## 9. 产物清单

- scripts/sweep063_ghidra_export.py、sweep063_driver.py、sweep063_probe.py、sweep063_spec.json、sweep063_spec2.json
- evidence/sweep063/ghidra/{winmine,sol,cards,usbdeview,procexp64}/（37 文件：tsv/txt/decomp/*.c）
- evidence/sweep063/shots_png/winmine_health_sweep063.png
