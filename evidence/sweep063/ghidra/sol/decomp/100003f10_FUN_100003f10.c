
undefined8 FUN_100003f10(longlong param_1,undefined8 *param_2)

{
  undefined8 uVar1;
  longlong *plVar2;
  int iVar3;
  
  uVar1 = FUN_100001690(*param_2,0,0);
  if ((*(int *)(param_1 + 0x30) != 0) && (iVar3 = 0, 0 < *(int *)(param_1 + 0x70))) {
    plVar2 = (longlong *)(param_1 + 0x78);
    do {
      (**(code **)(*plVar2 + 8))(*plVar2);
      iVar3 = iVar3 + 1;
      plVar2 = plVar2 + 1;
    } while (iVar3 < *(int *)(param_1 + 0x70));
  }
  FUN_100001690(uVar1,0,0);
  return 1;
}


