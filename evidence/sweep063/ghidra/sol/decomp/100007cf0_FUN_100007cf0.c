
longlong FUN_100007cf0(undefined8 *param_1,undefined4 param_2,LPRECT param_3,undefined8 *param_4)

{
  int iVar1;
  ulonglong uVar2;
  undefined8 uVar3;
  longlong lVar4;
  longlong *plVar5;
  
  iVar1 = (int)param_4;
  switch(param_2) {
  case 3:
    goto switchD_100007d24_caseD_3;
  default:
    uVar3 = FUN_1000047a0(param_1,param_2,(undefined8 *)param_3,iVar1);
    return (longlong)(int)uVar3;
  case 6:
    uVar3 = FUN_1000047a0(param_1,6,(undefined8 *)param_3,iVar1);
    if ((int)uVar3 != 0) {
      return 1;
    }
switchD_100007d24_caseD_3:
    uVar3 = FUN_100006f40(param_1,(undefined8 *)param_3);
    return (longlong)(int)uVar3;
  case 8:
    uVar2 = FUN_100006d00(param_1,(int)param_3);
    return (longlong)(int)uVar2;
  case 0xc:
    lVar4 = 2;
    plVar5 = param_1 + 0x11;
    do {
      if (*(int *)(*plVar5 + 0x28) != 0xd) {
        return 0;
      }
      lVar4 = lVar4 + 1;
      plVar5 = plVar5 + 1;
    } while (lVar4 < 6);
    return 1;
  case 0xd:
    iVar1 = FUN_100007170(param_1);
    return (longlong)iVar1;
  case 0xe:
    uVar3 = FUN_100007650(param_1,(undefined8 *)param_3,param_4);
    return (longlong)(int)uVar3;
  case 0xf:
    uVar2 = FUN_1000076f0(param_1,(int)param_3,iVar1);
    return (longlong)(int)uVar2;
  case 0x10:
    uVar3 = FUN_1000078a0((longlong)param_1,param_3);
    return (longlong)(int)uVar3;
  case 0x11:
    uVar3 = FUN_100007800(param_1);
    return (longlong)(int)uVar3;
  case 0x12:
    iVar1 = FUN_100007550(param_1);
    return (longlong)iVar1;
  }
}


