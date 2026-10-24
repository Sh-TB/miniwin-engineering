
/* WARNING: Removing unreachable block (ram,0x0001000078b8) */
/* WARNING: Removing unreachable block (ram,0x000100007919) */
/* WARNING: Removing unreachable block (ram,0x000100007949) */
/* WARNING: Removing unreachable block (ram,0x000100007938) */
/* WARNING: Removing unreachable block (ram,0x00010000793e) */
/* WARNING: Removing unreachable block (ram,0x000100007905) */
/* WARNING: Removing unreachable block (ram,0x0001000078e6) */
/* WARNING: Removing unreachable block (ram,0x000100007ab3) */
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

int entry(void)

{
  int _Code;
  byte *pbVar2;
  byte *pbVar1;
  uint uVar3;
  undefined4 local_res10 [2];
  undefined1 local_8c [4];
  undefined1 local_88 [8];
  undefined1 local_80 [8];
  _STARTUPINFOA local_78;
  
  GetStartupInfoA(&local_78);
  __set_app_type(2);
  _DAT_10000a350 = 0xffffffffffffffff;
  _DAT_10000a358 = 0xffffffffffffffff;
  *(undefined4 *)_fmode_exref = DAT_10000986c;
  *(undefined4 *)_commode_exref = DAT_100009868;
  FUN_100007b50();
  if (DAT_1000092b0 == 0) {
    __setusermatherr(FUN_100007b50);
  }
  _initterm(&DAT_1000013c8,&DAT_1000013d0);
  local_res10[0] = DAT_100009864;
  __getmainargs(local_8c,local_80,local_88,DAT_100009860,local_res10);
  _initterm(&DAT_1000013b0);
  pbVar1 = *(byte **)_acmdln_exref;
  if (*pbVar1 == 0x22) {
    do {
      pbVar2 = pbVar1;
      pbVar1 = pbVar2 + 1;
      if (*pbVar1 == 0) break;
    } while (*pbVar1 != 0x22);
    if (*pbVar1 == 0x22) {
      pbVar1 = pbVar2 + 2;
    }
  }
  else {
    for (; 0x20 < *pbVar1; pbVar1 = pbVar1 + 1) {
    }
  }
  for (; (*pbVar1 != 0 && (*pbVar1 < 0x21)); pbVar1 = pbVar1 + 1) {
  }
  if (((byte)local_78.dwFlags & 1) == 0) {
    uVar3 = 10;
  }
  else {
    uVar3 = (uint)local_78.wShowWindow;
  }
  _Code = FUN_100002920((HINSTANCE)&IMAGE_DOS_HEADER_100000000,0,pbVar1,uVar3);
                    /* WARNING: Subroutine does not return */
  exit(_Code);
}


