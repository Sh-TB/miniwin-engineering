# SWEEP-064 — SW-1292 判决 + SW-1204b 序号交叉验证

日期：2026-10-24 ｜ 基线：origin/main = 71febd8（SWEEP-063），fetch 核查 IN SYNC
工具源：Ghidra 12.2 导出（evidence/sweep063/ghidra/）+ wine-mirror spec（GitHub fetch 2026-10-24）

## 1. SW-1292 判决 —「引擎内缺口」是幽灵（探针误读）

Ghidra 反编译（SWEEP-063）显示 `FUN_100003f10` 为 save/restore 语义：

```c
uVar1 = FUN_100001690(*param_2, 0, 0);   // [c2a0] <- ps.hdc（保存旧值）
for each pile: (**(code**)(*pile + 8))(pile);  // 13 堆绘制（guest->guest cards.dll）
FUN_100001690(uVar1, 0, 0);              // [c2a0] <- 旧值（还原，若原为 NULL 则写回 NULL）
```

**推论**：paint 后读 [c2a0]=NULL 是**正确稳态**——不管绘制链是否运行。旧判据（跨
dispatch 保持 NULL + 零 guest API 调用）全部被此语义解释：DC 交换是纯 store（无
API），堆牌绘制走 guest→guest cards.dll（API 探针口径外）。

**判决实验**：sol 图形泵运行（MW_GUI_EVENTS=1）→ 38 帧（= SWEEP-061 全 dealt 基线）
→ 终帧截图 evidence/sweep064/shots_png/sol_board_sweep064.png 显示**完整棋盘**：
绿绒面、青蛙背牌堆、4 空基础堆、7 列真实牌面瀑布（2♥ K♥ Q♣ K♣ 6♦ J♦ 4♦）、状态栏。

**SW-1292 CLOSED（no-op 假设被否证——链是活的，探针读点选错）**。
SW-1292b 探针车厢随之取消（不再需要 PC 采样）。

## 2. SW-1204b — procexp64 序号全表独立交叉验证 ✓ + 1 处修正

方法：Ghidra 12.2 枚举 procexp64 导入表（767 条）中全部 ordinal 标记项 →
wine-mirror spec（GitHub raw，2026-10-24 fetch）命名 → 与引擎 ole_ord[] map 对照。

| DLL.ord | Wine spec 命名 | 引擎 map | 一致? |
|---------|----------------|----------|-------|
| OLEAUT32.2/4/6/7/8/9/12/19/20/23/24/25/150 | SysAllocString/SysAllocStringLen/SysFreeString/SysStringLen/VariantInit/VariantClear/VariantChangeType/SafeArrayGetUBound/SafeArrayGetLBound/SafeArrayAccessData/SafeArrayUnaccessData/SafeArrayPutElement/SysAllocStringByteLen | 同 | ✓ |
| OLEAUT32.16 | SafeArrayDestroy | SafeArrayDestroy (SW-1223) | ✓ |
| OLEAUT32.277 | VarUI4FromStr | VarUI4FromStr (SW-1225) | ✓ |
| WS2_32.8/9/14/15/51/56/115 | htonl/htons/ntohl/ntohs/gethostbyaddr/getservbyport/WSAStartup | 同 | ✓ |
| SHLWAPI.176 | IUnknown_QueryService | 同 (SWEEP-033) | ✓ |
| COMCTL32.345 | TaskDialogIndirect | 同 (SW-1218) | ✓ |
| COMCTL32.410/413 | SetWindowSubclass/DefSubclassProc | 同 (SWEEP-032) | ✓ |
| ACLUI.1 | CreateSecurityPage（spec @ 自动序号 1） | 同 | ✓ |
| **COMCTL32.17** | **InitCommonControls** | ~~ImageList_Create~~ | **✗ → 已修正** |

**COMCTL32.17 修正**（loader.c ole_ord[]）：SWEEP-014 的 ImageList_Create 是「槽位
邻域」猜测，无命名来源（违反 Rule 1）；wine comctl32.spec 第 21 行
`17 stdcall InitCommonControls()` 为权威序号表。**旁证**：Ghidra 导入表显示
usbdeview 同时导入 `Ordinal_6`（=CreateStatusWindowA ✓）与**按名字**的
ImageList_Create/ImageList_Add/ImageList_AddMasked——NirSoft 需要 ImageList 系列
时按名导入，序号 17 不可能是 ImageList_Create。

**回归**（修正后重建）：winmine 82 帧 / sol 38 帧 / freecell 10 / spider 630 /
calc 1 / notepad rc=0 —— 全部与基线一致，零回归。

## 3. 新增收获

- SW-1292 结案释放的下一步产能 → 转投 NirSoft 32 位（SW-1293）与语料扩展
- Ghidra 导出表已是常设交叉验证源：任何新序号命名必须同时给出 spec 来源
  （Rule 1）并可与 Ghidra 枚举对照
