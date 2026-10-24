
/* WARNING: Function: __security_check_cookie replaced with injection: security_check_cookie */
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

LRESULT FUN_100001bc0(HWND param_1,uint param_2,ulonglong param_3,undefined8 *param_4)

{
  ushort uVar1;
  int iVar2;
  int iVar3;
  HMENU hMenu;
  time_t tVar4;
  HWND pHVar5;
  INT_PTR IVar6;
  LRESULT LVar7;
  uint uVar8;
  undefined4 uVar9;
  UINT UVar10;
  int local_128;
  int local_124;
  tagPAINTSTRUCT local_118 [2];
  undefined8 local_48;
  
  local_48 = DAT_10000c260;
  uVar1 = (ushort)(param_3 >> 0x10);
  if (param_2 < 0x10) {
    if (param_2 == 0xf) {
      if (DAT_10000c294 == 0) {
        BeginPaint(param_1,local_118);
        if (DAT_10000c298 != (undefined8 *)0x0) {
          (*(code *)*DAT_10000c298)(DAT_10000c298,7,local_118);
        }
        EndPaint(param_1,local_118);
        return 0;
      }
      goto LAB_1000021ba;
    }
    switch(param_2) {
    case 2:
      KillTimer(DAT_10000ca28,0x29a);
      (*(code *)*DAT_10000c298)(DAT_10000c298,1,0);
      FUN_100003630(1);
      cdtTerm();
      DeleteObject(DAT_10000ca80);
      PostQuitMessage(0);
      break;
    default:
switchD_100001c2b_caseD_4:
      if (param_2 == DAT_10000c280) {
        if (param_3 == 1) {
          return (longlong)CONCAT22((undefined2)DAT_10000c9ec,(short)DAT_10000c9e8);
        }
        if (param_3 == 2) {
          iVar3 = cdtDraw(*param_4,*(undefined4 *)(param_4 + 1),
                          *(undefined4 *)((longlong)param_4 + 0xc),*(undefined4 *)(param_4 + 2),
                          *(undefined4 *)((longlong)param_4 + 0x14),*(undefined4 *)(param_4 + 3));
          return (longlong)iVar3;
        }
        if (param_3 == 3) {
          PostMessageW(DAT_10000ca28,0x112,0xf060,0);
          return 1;
        }
      }
      break;
    case 5:
      DAT_10000c294 = IsIconic(param_1);
      GetClientRect(param_1,(LPRECT)&DAT_10000ca40);
      iVar3 = (int)(short)param_4 + (short)DAT_10000c9e8 * -7;
      iVar3 = (int)((iVar3 >> 0x1f & 7U) + iVar3) >> 3;
      iVar2 = ((int)(CONCAT22(DAT_10000c9e8._2_2_,(short)DAT_10000c9e8) +
                    ((int)DAT_10000c9e8._2_2_ >> 0xf & 7U)) >> 3) + 3;
      if (iVar3 < iVar2) {
        if (DAT_10000ca20 == iVar2) goto switchD_100001c2b_caseD_3;
        iVar3 = iVar2;
      }
      DAT_10000ca20 = iVar3;
      FUN_100005ec0();
      InvalidateRect(param_1,(RECT *)0x0,1);
    case 3:
switchD_100001c2b_caseD_3:
      FUN_100008c40();
      break;
    case 6:
      if (((short)param_3 != 0) && (uVar1 == 0)) {
        FUN_100001620(param_1);
      }
      break;
    case 8:
      if (*(int *)(DAT_10000c298 + 0xb) != 0) {
        (*(code *)*DAT_10000c298)(DAT_10000c298,4,0);
      }
    case 7:
      ShowCursor((uint)(param_2 == 7));
    }
  }
  else {
    if (param_2 < 0x201) {
      if (param_2 != 0x200) {
        if (param_2 == 0x53) {
          LoadStringA(DAT_10000ca60,0x69,(LPSTR)local_118,100);
          pHVar5 = GetDesktopWindow();
          IVar6 = HtmlHelpA(pHVar5,local_118,0,0);
          if (IVar6 == 0) {
            FUN_100003470(0x12d);
          }
          goto LAB_1000021ba;
        }
        if (param_2 == 0x100) {
          (*(code *)*DAT_10000c298)(DAT_10000c298,2);
          goto LAB_1000021ba;
        }
        if (param_2 == 0x111) {
          uVar8 = (uint)param_3 & 0xffff;
          if (uVar8 < 0x3ed) {
            if (uVar8 == 0x3ec) {
              PostMessageW(param_1,0x112,0xf060,0);
            }
            else if (uVar8 == 1000) {
              tVar4 = time((time_t *)0x0);
              _DAT_10000ca34 = (uint)tVar4 & 0x7fff;
              srand(_DAT_10000ca34);
              (*(code *)*DAT_10000c298)(DAT_10000c298,8,0);
            }
            else if (uVar8 == 0x3e9) {
              (*(code *)*DAT_10000c298)(DAT_10000c298,9,0);
            }
            else if (uVar8 == 0x3ea) {
              FUN_100008a40();
            }
            else if (uVar8 == 0x3eb) {
              FUN_1000085e0();
            }
          }
          else if (uVar8 == 0x3f2) {
            (*(code *)*DAT_10000c298)(DAT_10000c298,0x12,0);
          }
          else if (uVar8 == 2000) {
            FUN_100003060((LPWSTR)local_118,0x6c,100);
            ShellAboutW(param_1,(LPCWSTR)&DAT_10000ca00,(LPCWSTR)local_118,DAT_10000ca18);
          }
          else if (uVar8 - 0xffe2 < 3) {
            FUN_100001820((int)(short)param_3);
          }
          goto LAB_1000021ba;
        }
        if (param_2 == 0x116) {
          hMenu = GetMenu(param_1);
          if ((*(int *)(DAT_10000c298 + 1) == 0) || (*(int *)((longlong)DAT_10000c298 + 100) != -1))
          {
            UVar10 = 3;
          }
          else {
            UVar10 = 0;
          }
          EnableMenuItem(hMenu,0x3e9,UVar10);
          UVar10 = 0;
          if (*(int *)((longlong)DAT_10000c298 + 100) != -1) {
            UVar10 = 3;
          }
          EnableMenuItem(hMenu,1000,UVar10);
          UVar10 = 0;
          if (*(int *)((longlong)DAT_10000c298 + 100) != -1) {
            UVar10 = 3;
          }
          EnableMenuItem(hMenu,0x3ea,UVar10);
          UVar10 = 0;
          if (*(int *)((longlong)DAT_10000c298 + 100) != -1) {
            UVar10 = 3;
          }
          EnableMenuItem(hMenu,2000,UVar10);
          goto LAB_1000021ba;
        }
        if (param_2 == 0x11f) {
          if ((((param_3 >> 0x10 & 0x10) == 0) && ((uVar1 >> 0xd & 1) == 0)) &&
             ((uVar1 >> 0xb & 1) == 0)) {
            FUN_100008ec0((uint)param_3 & 0xffff);
          }
          else {
            FUN_100008ec0(0x1fff);
          }
          goto LAB_1000021ba;
        }
        goto switchD_100001c2b_caseD_4;
      }
      uVar9 = 5;
      iVar3 = *(int *)(DAT_10000c298 + 0xb);
joined_r0x000100002179:
      if (iVar3 == 0) goto LAB_1000021ba;
    }
    else if (param_2 == 0x201) {
      SetCapture(param_1);
      if (*(int *)(DAT_10000c298 + 0xb) != 0) goto LAB_1000021ba;
      uVar9 = 3;
    }
    else {
      if (param_2 == 0x202) {
        ReleaseCapture();
        uVar9 = 4;
        iVar3 = *(int *)(DAT_10000c298 + 0xb);
        goto joined_r0x000100002179;
      }
      if (param_2 == 0x203) {
        uVar9 = 6;
        if (*(int *)(DAT_10000c298 + 0xb) != 0) goto LAB_1000021ba;
      }
      else {
        if (param_2 != 0x204) goto switchD_100001c2b_caseD_4;
        pHVar5 = GetCapture();
        if (pHVar5 != (HWND)0x0) goto LAB_1000021ba;
        uVar9 = 0x13;
      }
    }
    local_128 = (int)(short)param_4;
    local_124 = (int)(short)((ulonglong)param_4 >> 0x10);
    (*(code *)*DAT_10000c298)(DAT_10000c298,uVar9,&local_128,0);
  }
LAB_1000021ba:
  LVar7 = DefWindowProcW(param_1,param_2,param_3,(LPARAM)param_4);
  return LVar7;
}


