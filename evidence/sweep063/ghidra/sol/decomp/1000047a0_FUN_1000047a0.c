
undefined8 FUN_1000047a0(undefined8 *param_1,undefined4 param_2,undefined8 *param_3,int param_4)

{
  undefined8 uVar1;
  int iVar2;
  
  iVar2 = (int)param_3;
  switch(param_2) {
  case 0:
    uVar1 = FUN_100003910((longlong)param_1,iVar2);
    return uVar1;
  case 1:
    FUN_1000034e0(param_1);
    break;
  case 2:
    uVar1 = FUN_100004370(param_1,iVar2);
    return uVar1;
  case 3:
    uVar1 = FUN_100003960((longlong)param_1,param_3,param_4);
    return uVar1;
  case 4:
    uVar1 = FUN_100003a30(param_1,param_3,param_4);
    return uVar1;
  case 5:
    uVar1 = FUN_100003df0((longlong)param_1,param_3);
    return uVar1;
  case 6:
    uVar1 = FUN_100003b80((longlong)param_1,param_3);
    return uVar1;
  case 7:
    uVar1 = FUN_100003f10((longlong)param_1,param_3);
    return uVar1;
  case 9:
    uVar1 = FUN_100003fc0(param_1);
    return uVar1;
  case 10:
    uVar1 = FUN_100004080((longlong)param_1,iVar2,param_4);
    return uVar1;
  case 0xb:
    *(undefined4 *)(param_1 + 1) = 0;
    return 0;
  case 0xd:
    uVar1 = FUN_100004750((longlong)param_1);
    return uVar1;
  case 0xe:
    return 1;
  case 0xf:
    uVar1 = FUN_1000046d0((longlong)param_1,iVar2,param_4);
    return uVar1;
  case 0x12:
    FUN_100003030();
    return 0;
  case 0x13:
    uVar1 = FUN_100003c00((longlong)param_1);
    return uVar1;
  }
  return 0;
}


