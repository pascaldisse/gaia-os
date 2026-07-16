// AArch64 pass 7-9/10 driver for Shrine IC.
// Family generators append encoded aarch64 words to tmpi->ic_body.

Bool A64ICIsBranch(CIntermediateCode *tmpi)
{
  switch (tmpi->ic_code) { // bug#5: bounded — sparse cases, ic_code may be any opcode
    case IC_JMP: //Shrine 0x51, unconditional
    case IC_BR_ZERO:
    case IC_BR_NOT_ZERO:
    case IC_BR_EQU_EQU:
    case IC_BR_NOT_EQU:
    case IC_BR_LESS:
    case IC_BR_GREATER_EQU:
    case IC_BR_GREATER:
    case IC_BR_LESS_EQU:
    case 0x8F:
    case 0x90:
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case IC_BR_AND_ZERO:
    case IC_BR_AND_NOT_ZERO:
    case 0x97:
    case 0x98:
    case 0x9D:
    case 0x9E:
    case 0x9F:
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA3:
    case 0xA4:
      return TRUE;
  }
  return FALSE;
}

Bool A64GenIC(CCmpCtrl *cc,CIntermediateCode *tmpi)
{
  if (A64GenInt(cc,tmpi))
    return TRUE;
  if (A64GenBr(cc,tmpi))
    return TRUE;
  if (A64GenF(cc,tmpi))
    return TRUE;
  return FALSE;
}

I64 OptPass789A64(CCmpCtrl *cc,COptReg *reg_offsets,U8 *buf,CDbgInfo **_dbg)
{/*cc->pass==7 is first time
cc->pass==8 is second time
cc->pass==9 is third time
cc->pass==9 is fourth time and repeated until size stops shrinking
size is now known
cc->pass==10 is final pass, code is placed into buf.
*/
  CIntermediateCode *tmpi,*tmpi_next;
  I64 i,cnt,num_lines=cc->max_line+1-cc->min_line,rip=0,last_rip=I64_MAX;
  U8 *ptr,*saved_ic_data;
  CCodeMisc *lb;
  CDbgInfo *dbg_info;
  CAOTCtrl *aotc=cc->aotc;
  CAOTAbsAddr *tmpa;

  a64_str_buf=buf; a64_arg_cnt=0; a64_arg_sp=0; a64_frame_argbase=0; //A64GenBr pass-entry state
  if (_dbg) {
    *_dbg=dbg_info=CAlloc(offset(CDbgInfo.body)+sizeof(U32)*(num_lines+1));
    dbg_info->min_line=cc->min_line;
    dbg_info->max_line=cc->max_line;
    if (cc->flags&CCF_AOT_COMPILE)
      dbg_info->body[0]=aotc->rip;
    else
      dbg_info->body[0]=buf;
  } else
    dbg_info=NULL;

  while (TRUE) {
    rip=0;
    tmpi=&cc->coc.coc_head;
    tmpi->ic_last_start=-1;
    tmpi->ic_cnt=0;
    tmpi=tmpi->next;
    while (tmpi->ic_code) {
      tmpi_next=tmpi->next;
      cnt=tmpi->ic_cnt;
      tmpi->ic_last_start=rip;
      tmpi->ic_cnt=0;

      saved_ic_data=tmpi->ic_data;
      switch (tmpi->ic_code) { // bug#5: bounded — only NOP/LABEL cased, rest hits default
	case IC_NOP1:
	case IC_NOP2:
	  cnt=0;
	  break;
	case IC_LABEL:
	  lb=tmpi->ic_data;
	  lb->addr=rip;
	  if (lb->type==CMT_ASM_LABEL)
	    lb->addr+=lb->rip;
	  cnt=0;
	  break;
	default:
	  if (A64ICIsBranch(tmpi)) {
	    lb=OptLabelFwd(saved_ic_data);
	    if (lb->addr!=INVALID_PTR)
	      tmpi->ic_data=lb->addr-rip;
	    else
	      tmpi->ic_data=0;
	  }
	  if (!A64GenIC(cc,tmpi)) {
	    "zoneC UNHANDLED ic=%02X\n",tmpi->ic_code; //TEMP-PROBE spec90
	    throw('BackA64');
	  }
	  if (tmpi->ic_flags & ICF_PUSH_RES)
	    a64_arg_cnt++; //bug#7d: explicit-call args ride ICF_PUSH_RES/MDF_STK stores (A64StoreRes), not IC_PUSH
	  tmpi->ic_data=saved_ic_data;
	  cnt=tmpi->ic_cnt;
      }
      if (cnt && buf)
	MemCpy(buf+rip,tmpi->ic_body,cnt);
      if (dbg_info && cc->min_line<=tmpi->ic_line<=cc->max_line) {
	i=tmpi->ic_line-cc->min_line;
	if (!dbg_info->body[i]) {
	  if (cc->flags&CCF_AOT_COMPILE)
	    dbg_info->body[i]=rip+aotc->rip;
	  else
	    dbg_info->body[i]=rip+buf;
	}
      }
      rip+=cnt;
      if (tmpi->ic_cnt>=IC_BODY_SIZE)
	throw('Compiler');
      tmpi=tmpi_next;
    }

    lb=cc->coc.coc_next_misc;
    while (lb!=&cc->coc.coc_next_misc) {
      switch (lb->type) {
	case CMT_STR_CONST:
	  lb->addr=rip;
	  if (buf)
	    MemCpy(buf+rip,lb->str,lb->st_len);
	  rip+=lb->st_len;
	  break;
	case CMT_JMP_TABLE:
	  lb->addr=rip;
	  ptr=buf+lb->addr;
	  if (lb->flags&(CMF_I8_JMP_TABLE|CMF_U8_JMP_TABLE)) {
	    if (buf)
	      for (i=0;i<lb->range;i++)
		*ptr++=lb->jmp_table[i]->addr-lb->begin->addr;
	    rip+=lb->range;
	  } else if (lb->flags&(CMF_I16_JMP_TABLE|CMF_U16_JMP_TABLE)) {
	    if (buf)
	      for (i=0;i<lb->range;i++)
		*ptr(U16 *)++=lb->jmp_table[i]->addr-lb->begin->addr;
	    rip+=lb->range<<1;
	  } else {
	    if (buf)
	      for (i=0;i<lb->range;i++) {
		if (cc->flags&CCF_AOT_COMPILE && !(cc->flags&CCF_NO_ABSS)) {
		  tmpa=CAlloc(sizeof(CAOTAbsAddr));
		  tmpa->next=aotc->abss;
		  tmpa->type=AAT_ADD_U32;
		  aotc->abss=tmpa;
		  tmpa->rip=aotc->rip+lb->addr+i<<2;
		  *ptr(U32 *)++=lb->jmp_table[i]->addr+aotc->rip;
		} else
		  *ptr(U32 *)++=lb->jmp_table[i]->addr+buf;
	      }
	    rip+=lb->range<<2;
	  }
	  break;
	case CMT_FLOAT_CONSTS:
	  lb->addr=rip;
	  if (buf)
	    MemCpy(buf+lb->addr,lb->float_consts,lb->num_consts*sizeof(F64));
	  rip+=lb->num_consts*sizeof(F64);
	  break;
      }
      lb=lb->next;
    }
    if (last_rip<=rip)
      break;
    last_rip=rip;
  }

  if (dbg_info) {
    if (cc->flags&CCF_AOT_COMPILE)
      dbg_info->body[num_lines]=rip+aotc->rip;
    else
      dbg_info->body[num_lines]=rip+buf;
  }
  return rip;
}
