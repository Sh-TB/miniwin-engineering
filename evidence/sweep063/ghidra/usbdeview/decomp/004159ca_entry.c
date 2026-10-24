
/* WARNING: Function: __SEH_epilog replaced with injection: EH_epilog3 */
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

int entry(void)

{
  HMODULE pHVar1;
  undefined4 *puVar2;
  int iVar3;
  int *piVar4;
  int unaff_EBP;
  byte *pbVar5;
  undefined4 uVar6;
  
  FUN_00415bb4(&DAT_004164e0,0x70);
  pHVar1 = GetModuleHandleA((LPCSTR)0x0);
  if (((short)pHVar1->unused == 0x5a4d) &&
     (piVar4 = (int *)((int)&pHVar1->unused + pHVar1[0xf].unused), *piVar4 == 0x4550)) {
    if ((short)piVar4[6] == 0x10b) {
      if (0xe < (uint)piVar4[0x1d]) {
        iVar3 = piVar4[0x3a];
        goto LAB_00415a2d;
      }
    }
    else if (((short)piVar4[6] == 0x20b) && (0xe < (uint)piVar4[0x21])) {
      iVar3 = piVar4[0x3e];
LAB_00415a2d:
      *(uint *)(unaff_EBP + -0x1c) = (uint)(iVar3 != 0);
      goto LAB_00415a33;
    }
  }
  *(undefined4 *)(unaff_EBP + -0x1c) = 0;
LAB_00415a33:
  *(undefined4 *)(unaff_EBP + -4) = 0;
  __set_app_type(2);
  _DAT_0041b53c = 0xffffffff;
  DAT_0041b540 = 0xffffffff;
  puVar2 = (undefined4 *)__p__fmode();
  *puVar2 = DAT_0041a6ac;
  puVar2 = (undefined4 *)__p__commode();
  *puVar2 = DAT_0041a6a8;
  _DAT_0041b538 = *(undefined4 *)_adjust_fdiv_exref;
  FUN_00408e52();
  if (DAT_0041a000 == 0) {
    __setusermatherr(FUN_00408e52);
  }
  FUN_00415ba2();
  initterm(&DAT_004164b4,&DAT_004164b8);
  *(undefined4 *)(unaff_EBP + -0x20) = DAT_0041a6a4;
  iVar3 = __getmainargs((int *)(unaff_EBP + -0x2c),(char ***)(unaff_EBP + -0x28),
                        (char ***)(unaff_EBP + -0x24),DAT_0041a6a0,
                        (_startupinfo *)(unaff_EBP + -0x20));
  *(int *)(unaff_EBP + -0x30) = iVar3;
  initterm(&DAT_0041647c,&DAT_004164b0);
  pbVar5 = *(byte **)_acmdln_exref;
  *(byte **)(unaff_EBP + -0x34) = pbVar5;
  if (*pbVar5 != 0x22) {
    do {
      if (*pbVar5 < 0x21) goto LAB_00415b01;
      pbVar5 = pbVar5 + 1;
      *(byte **)(unaff_EBP + -0x34) = pbVar5;
    } while( true );
  }
  do {
    pbVar5 = pbVar5 + 1;
    *(byte **)(unaff_EBP + -0x34) = pbVar5;
    if (*pbVar5 == 0) break;
  } while (*pbVar5 != 0x22);
  if (*pbVar5 != 0x22) goto LAB_00415b01;
  do {
    pbVar5 = pbVar5 + 1;
    *(byte **)(unaff_EBP + -0x34) = pbVar5;
LAB_00415b01:
  } while ((*pbVar5 != 0) && (*pbVar5 < 0x21));
  *(undefined4 *)(unaff_EBP + -0x4c) = 0;
  GetStartupInfoA((LPSTARTUPINFOA)(unaff_EBP + -0x78));
  uVar6 = 0;
  pHVar1 = GetModuleHandleA((LPCSTR)0x0);
  iVar3 = FUN_0040ebc1(pHVar1,uVar6,(char *)pbVar5);
  *(int *)(unaff_EBP + -0x7c) = iVar3;
  if (*(int *)(unaff_EBP + -0x1c) == 0) {
                    /* WARNING: Subroutine does not return */
    exit(iVar3);
  }
  _cexit();
  *(undefined4 *)(unaff_EBP + -4) = 0xffffffff;
  return iVar3;
}


