/*
 * ShrineHost.HH
 * zoneC C4 hosted extract of Shrine kernel definitions.
 * Generated mechanically from shrine/Kernel/KernelA.HH, KernelB.HH, KernelC.HH.
 */
// ALIASED (host definitions): CTask, CDocEntry, CDoc
// --- from KernelA.HH ---
#define NULL	0
#define TRUE	1
#define FALSE	0
#define I8_MIN (-0x80)
#define I8_MAX 0x7F
#define I16_MIN (-0x8000)
#define I16_MAX 0x7FFF
#define I32_MIN (-0x80000000)
#define I32_MAX 0x7FFFFFFF
#define I64_MIN (-0x8000000000000000)
#define I64_MAX 0x7FFFFFFFFFFFFFFF
#define U64_MIN 0
#define U64_MAX 0xFFFFFFFFFFFFFFFF
#define INVALID_PTR	I64_MAX
#define STR_LEN 144
public class CQueVectU8
{
  CQueVectU8	*next,*last;
  I64	total_cnt,node_cnt,min_idx;
  U8	body[QUE_VECT_U8_CNT];
};
#define BIN_SIGNATURE_VAL	'TOSB'
class CBinFile
{//$LK,"Bin File Header Generation",A="FF:::/Compiler/CMain.HC,16 ALIGN"$ by compiler.
  U16	jmp;
  U8	module_align_bits,
	reserved;
  U32	bin_signature;
  I64	org,
	patch_table_offset, //$LK,"Patch Table Generation",A="FF:::/Compiler/CMain.HC,IET_ABS_ADDR"$
	file_size;
};
#define IET_END			0
#define IET_REL_I0		2 //Fictitious
#define IET_IMM_U0		3 //Fictitious
#define IET_REL_I8		4
#define IET_IMM_U8		5
#define IET_REL_I16		6
#define IET_IMM_U16		7
#define IET_REL_I32		8
#define IET_IMM_U32		9
#define IET_REL_I64		10
#define IET_IMM_I64		11
#define IEF_IMM_NOT_REL		1
#define IET_REL32_EXPORT	16
#define IET_IMM32_EXPORT	17
#define IET_REL64_EXPORT	18 //Not implemented
#define IET_IMM64_EXPORT	19 //Not implemented
#define IET_ABS_ADDR		20
#define IET_DATA_HEAP		23
#define IET_MAIN		25
#define RLf_DOC			13
public class CHash
{//See $LK,"Hash",A="HI:Hash"$
  CHash	*next;
  U8	*str;
  U32	type,
	use_cnt; // inc'ed every time search found, never dec'ed.
};
public class CHashTable
{
  CHashTable *next;
  I64	mask,locked_flags;
  CHash	**body;
};
#define HTt_EXPORT_SYS_SYM	0
#define HTt_GLBL_VAR		3
#define HTt_CLASS		4
#define HTt_FUN			6
#define HTt_KEYWORD		9
#define HTf_PUBLIC		24
#define HTT_EXPORT_SYS_SYM	0x00001 //CHashExport
#define HTT_IMPORT_SYS_SYM	0x00002 //CHashImport
#define HTT_DEFINE_STR		0x00004 //CHashDefineStr
#define HTT_GLBL_VAR		0x00008 //CHashGlblVar
#define HTT_CLASS		0x00010 //CHashClass
#define HTT_INTERNAL_TYPE	0x00020 //CHashClass
#define HTT_FUN			0x00040 //CHashFun
#define HTT_KEYWORD		0x00200 //CHashGeneric $LK,"KEYWORD",A="FF:::/Compiler/OpCodes.DD,KEYWORD"$
#define HTT_ASM_KEYWORD		0x00400 //CHashGeneric $LK,"ASM_KEYWORD",A="FF:::/Compiler/OpCodes.DD,ASM_KEYWORD"$
#define HTT_OPCODE		0x00800 //CHashOpcode
#define HTT_HELP_FILE		0x08000 //CHashSrcSym
#define HTG_TYPE_MASK		0x1FFFF
#define HTF_PUBLIC		0x01000000
#define HTF_EXPORT		0x02000000
#define HTF_IMPORT		0x04000000
#define HTF_IMM			0x08000000
#define HTF_GOTO_LABEL		0x10000000
#define HTF_RESOLVE		0x20000000
#define HTF_UNRESOLVED		0x40000000
#define HTF_LOCAL		0x80000000
#define HTG_ALL 		-1
class CDbgInfo
{
  U32	min_line,max_line;
  U8 *body[1]; // zoneC C4: widened from U32 for 64-bit hosted heap
};
public class CHashSrcSym:CHash
{
  U8	*src_link,
	*idx;
  CDbgInfo *dbg_info;
  U8	*import_name;
  CAOTImportExport *ie_lst;
};
public class CHashGeneric:CHash
{
  I64	user_data0,user_data1,user_data2;
};
public class CHashExport:CHashSrcSym
{
  I64	val;
};
#define PTR_STARS_NUM	4
#define MLF_DFT_AVAILABLE	1
#define MLF_LASTCLASS		2
#define MLF_STR_DFT_AVAILABLE	4
#define MLF_FUN			8
#define MLF_DOT_DOT_DOT		16
#define MLF_NO_UNUSED_WARN	32
#define MLF_STATIC		64
public class CArrayDim
{
  CArrayDim *next;
  I64	cnt,total_cnt;
};
#define MLMF_IS_STR	1
public class CMemberLstMeta
{
  CMemberLstMeta *next;
  U8	*str;
  I64	flags,
	user_data;
};
public class CMemberLst
{
  CMemberLst *next,*left,*right,
	*left_class_base,*right_class_base; //For finding dup class local vars.
  U8	*str;
  CHashClass *member_class,*member_class_base;
  CMemberLstMeta *meta;
  U32	use_cnt;
  U16	flags;
  I8	reg,pad;
  I64	offset,size;
  CArrayDim dim;
  U8	*static_data;
  union {
    I64	static_data_rip;
    I64 dft_val;
  }
  CHashFun *fun_ptr;
};
class CExternUsage
{
  CExternUsage *next;
  I64	rip;
};
public class CHashDefineStr:CHashSrcSym
{
  U8	*data,**sub_idx;
  I64	cnt;
};
#define Cf_EXTERN		0
#define Cf_INTERNAL_TYPE	1
public class CHashClass:CHashSrcSym
{//See $LK,"PrsClassNew",A="MN:PrsClassNew"$(). base_class of $LK,"CHashFun",A="MN:CHashFun"$
  I64	size,neg_offset;
  U32	member_cnt;
  U8	ptr_stars_cnt,raw_type;
  U16	flags;
  CMemberLst *member_lst_and_root, //Head of linked list and head of tree.
	*member_class_base_root, //For finding dup class local vars.
	*last_in_member_lst;
  CHashClass	*base_class,
	*fwd_class;
};
#define Ff_INTERRUPT		8
#define Ff_HASERRCODE		9
#define Ff_ARGPOP		10
#define Ff_NOARGPOP		11
#define Ff_INTERNAL		12
#define Ff__EXTERN		13
#define Ff_DOT_DOT_DOT		14
#define Ff_RET1			15
public class CHashFun:CHashClass
{//See $LK,"PrsFunNew",A="MN:PrsFunNew"$().
  CHashClass *return_class;
  U32	arg_cnt,pad,
	used_reg_mask,clobbered_reg_mask;
  U8	*exe_addr;
  CExternUsage *ext_lst;
};
#define GVF_FUN		1
#define GVF_IMPORT	2
#define GVF_EXTERN	4
#define GVF_DATA_HEAP	8
#define GVF_ALIAS	16
#define GVF_ARRAY	32
public class CHashGlblVar:CHashSrcSym
{
  I64	size,flags;
  CHashClass *var_class;
  CHashFun *fun_ptr;
  CArrayDim dim;
  U8	*data_addr;
  union {
    I64	data_addr_rip;
    CAOTHeapGlbl *heap_glbl;
  }
};
#define DOCT_TEXT		0
#define DOCT_NEW_LINE		1
#define DOCT_TAB		3
#define DOCT_CURSOR		5
#define DOCT_MARKER		6
#define DOCT_SHIFTED_Y		25
#define DOCT_INS_BIN		38
#define DOCT_INS_BIN_SIZE	39
#define DOCT_TYPES_NUM		43
#define DOCG_BL_IV_UL		0xB0000000
#define DOCEF_TAG		1
#define DOCEF_DEFINE		8
#define DOCEF_TAG_CB		0x100000000
#define DOCEF_SKIP		0x40000000000000
#define DOCEF_FILTER_SKIP	0x200000000000000
#define DOCEF_HIGHLIGHT		0x4000000
#define DOCEF_WORD_WRAP		0x8000000
#define DOCF_DBL_DOLLARS	0x20
#define OPTf_TRACE		0x01
#define OPTf_WARN_UNUSED_VAR	0x10 //Applied to funs, not stmts
#define OPTf_WARN_PAREN		0x11 //Warn unnecessary parens
#define OPTf_WARN_HEADER_MISMATCH 0x13
#define OPTf_EXTERNS_TO_IMPORTS	0x20
#define OPTf_KEEP_PRIVATE	0x21
#define OPTf_NO_REG_VAR		0x22  //Applied to funs, not stmts
#define OPTf_GLBLS_ON_DATA_HEAP	0x23
#define OPTF_ECHO		(1<<OPTf_ECHO)
#define RT_I0		2
#define RT_U0		3
#define RT_I8		4
#define RT_U8		5
#define RT_I16		6
#define RT_U16		7
#define RT_I32		8
#define RT_U32		9
#define RT_I64		10
#define RT_PTR		10 //Signed to allow negative err codes. $LK,"DOCM_CANCEL",A="MN:DOCM_CANCEL"$
#define RT_U64		11
#define RT_F32		12 //Not implemented
#define RT_F64		14
#define RTF_UNSIGNED	1
#define RTG_MASK	0xFF
#define MDF_NULL	0x0000
#define MDF_STK		0x0100
#define MDF_IMM		0x0200
#define MDF_REG		0x0400
#define MDF_DISP	0x0800
#define MDF_SIB		0x1000
#define MDF_RIP_DISP32	0x2000
#define MDG_MASK	0xFF00
#define MDG_REG_DISP_SIB_RIP	(MDF_REG|MDG_DISP_SIB_RIP)
#define ICF_RES_TO_F64			0x000000001
#define ICF_RES_TO_INT			0x000000002
#define ICF_ARG1_TO_F64			0x000000004
#define ICF_ARG1_TO_INT			0x000000008
#define ICF_ARG2_TO_F64			0x000000010
#define ICF_ARG2_TO_INT			0x000000020
#define ICF_USE_F64			0x000000040
#define ICF_USE_UNSIGNED		0x000000080
#define ICF_USE_INT			0x000000100 //highest priority
#define ICF_RES_NOT_USED		0x000000200
#define ICF_BY_VAL			0x000000800 //By value, not ref.
#define ICF_PUSH_RES			0x000002000
#define ICF_PASS_TRACE			0x000004000
#define ICF_RES_WAS_STK			0x000008000
#define ICF_ARG1_WAS_STK		0x000010000
#define ICF_ARG2_WAS_STK		0x000020000
#define ICF_PUSH_CMP			0x000040000 //for 50<i<j<=100 exps
#define ICF_POP_CMP			0x000080000 //for 50<i<j<=100 exps
#define ICF_SWAP			0x000100000
#define ICf_DONT_PUSH_FLOAT0		21 // 3bits
#define ICF_LOCK			0x010000000
#define ICF_NO_RIP			0x020000000
#define ICG_NO_CVT_MASK			0x1FFFFFF00
#define IC_BODY_SIZE			0x83
#define ECF_HAS_PUSH_CMP		0x01 //for 50<i<j<=100 exps
U16 class CICType
{
  U8	raw_type,mode;
};
class CICArg
{
  CICType type;
  U16	reg; //low is reg, high is index_reg+scale<<6
  I64	disp;
};
class CICTreeLinks
{
  CHashClass *arg1_class,*arg2_class;
  CIntermediateCode *arg1_tree,*arg2_tree;
  CHashClass *class2;
};
class CIntermediateCodeBase
{
  CIntermediateCode *next,*last;
  U16	ic_code,
	ic_precedence;
  I16	ic_cnt,
	ic_last_start;
};
class CIntermediateCode:CIntermediateCodeBase
{
  I64	ic_flags,
	ic_data,
	ic_line;
  CHashClass *ic_class,*ic_class2;
  CICArg arg1,arg2,res;
  U8	arg1_type_pointed_to; //Used for $LK,"IST_ASSIGN",A="MN:IST_ASSIGN"$ and $LK,"IST_DEREF",A="MN:IST_DEREF"$ $LK,"ic_codes",A="MN:intermediate_code_table"$.
  union {
    U8 ic_body[IC_BODY_SIZE];
    //Tree Links are created in OptPass012.  An ADD opcode, for example,
    //points back to its two earlier arg CIntermediateCode's.

    //Tree links get destroyed during Pass789A when they get overwrites
    //by machine code.	(Saves room to union the output machine code buffer
    //with these links, since they are not needed after pass4.

    //Tree links are used during passes 012 and 3 for determining types.
    CICTreeLinks t;
  };
class CPrsStk
{
  I64	ptr,
	stk[255],
	ptr2,
	stk2[255];
};
#define CMT_LABEL		0
#define CMT_ASM_LABEL		1
#define CMT_GOTO_LABEL		2
#define CMT_STR_CONST		3
#define CMT_JMP_TABLE		4
#define CMT_FLOAT_CONSTS	5
#define CMT_ARRAY_DIM		6
#define CMT_HASH_ENTRY		7
#define CMF_POP_CMP		0x01
#define CMF_DEFINED		0x02
#define CMF_I8_JMP_TABLE	0x04
#define CMF_U8_JMP_TABLE	0x08
#define CMF_I16_JMP_TABLE	0x10
#define CMF_U16_JMP_TABLE	0x20
#define CM_CONSTS_NUM		16
class CCodeMisc
{
  CCodeMisc *next,*last,*fwd,*dft,*begin;
  U8	*str;
  U32	type,flags;
  I64	use_cnt;
  U8	*addr;
  union {
    I64 st_len;		//STR_CONST
    I64 num_consts;	//FLOAT_CONSTS
    I64 range;
    I64 rip;		//ASM_LABEL
  }
  union {
    CCodeMisc **jmp_table;
    F64	*float_consts;
    CArrayDim *dim;
    CHash *h;
  };
class CInst
{
  U8	ins_entry_num, //This entry num in opcode hash entry
	opcode_cnt,
	opcode[4];
  U16	flags;
  U8	slash_val,uasm_slash_val,opcode_modifier,
	arg1,arg2,
	size1,size2,//Size in bits
	pad;
};
#define PUSH_REGS PUSH RAX PUSH RCX PUSH RDX PUSH RBX PUSH RBP PUSH RSI \
PUSH RDI PUSH R8 PUSH R9 PUSH R10 PUSH R11 PUSH R12 PUSH R13 PUSH R14 PUSH R15
#define POP_REGS POP R15 POP R14 POP R13 POP R12 POP R11 POP R10 POP R9 \
POP R8 POP RDI POP RSI POP RBP POP RBX POP RDX POP RCX POP RAX
#define REG_RAX		0
#define REG_RSP		4
#define REG_RBP		5
#define REG_R8		8
#define REG_REGS_NUM	16
#define REG_RIP		16	//Used by compiler, not really it's num
#define REG_NONE	32	//noreg flag sets it to this
#define REG_ALLOC	33	//reg flag sets it to this
#define REG_UNDEF	I8_MIN
#define REGG_CLOBBERED		0x013F //RAX,RCX,RDX,RBX,R8
#define REGG_SAVED		0x0030 //RBP,RSP
#define REGG_STK_TMP		0x0200 //R9
#define REGG_LOCAL_VARS		0xCCC0 //RSI,RDI,R10,R11,R14,R15
#define REGG_LOCAL_NON_PTR_VARS	0x3000 //R12,R13
#define AOT_BIN_BLK_BITS 16
#define AOT_BIN_BLK_SIZE (1<<AOT_BIN_BLK_BITS)
class CAOTBinBlk
{
  CAOTBinBlk *next;
  U8	body[AOT_BIN_BLK_SIZE];
};
I64 class CAbsCntsI64
{
  U16	abs_addres, //Only odd/even matters. Cnt of absolute addres in an exp.
	c_addres; //Only odd/even matters. Cnt of C addres in an exp.
  U32	externs; //Only nonzero matters. Some regions have externs banned.
};
class CAsmUndefHash
{//Only place created is $LK,"Exp Parser",A="FF:::/Compiler/PrsExp.HC,sizeof(CAsmUndefHash)"$ when an undef is found in an ASM exp.
  CAsmUndefHash	*next;
  CHashExport	*hash;
};
class CAsmNum
{
  I64	i;
  U8	*machine_code;
  CAsmUndefHash	*local_asm_undef_hash,*glbl_asm_undef_hash;
  CAbsCntsI64 abs_cnts;
};
class CAsmArg
{
  CAsmNum num;
  I64	seg,size, //Size in bytes
	reg1,reg2,
	reg1_type,reg2_type,
	scale;
  Bool	indirect,imm_or_off_present,just_seg,pad[5];
};
class CAsmUnresolvedRef
{
  CAsmUnresolvedRef *next;
  I64	type,line_num;
  U8	*machine_code;
  I64	rip,rel_rip;
  CAOT	*aot;
  U8	*str;	//Only for import glbls
  CAsmUndefHash *asm_undef_hash;
  Bool	U8_avail,
	imm_flag;//Only for import glbls
};
#define IEF_GOTO_LABEL		1
class CAOTImportExport
{
  CAOTImportExport *next,*last;
  I64	rip,flags;
  CAOT	*aot;
  U8	*str,
	*src_link,
	type,pad[7];
};
#define AAT_ADD_U8		0
#define AAT_SUB_U8		1
#define AAT_ADD_U16		2
#define AAT_SUB_U16		3
#define AAT_ADD_U32		4
#define AAT_SUB_U32		5
#define AAT_ADD_U64		6
#define AAT_SUB_U64		7
class CAOTAbsAddr
{
  CAOTAbsAddr *next;
  I64	rip;
  U8	type,pad[7];
};
class CAOTHeapGlblRef
{
  CAOTHeapGlblRef *next;
  I64	rip;
};
class CAOTHeapGlbl
{
  CAOTHeapGlbl *next;
  U8	*str;
  I64	size;
  CAOTHeapGlblRef *references;
};
class CAOT
{
  CAOT	*next,*last;
  U8	*buf;
  I64	rip,rip2,
	aot_U8s,
	max_align_bits,org;
  CAOT	*parent_aot;
  CAOTImportExport *next_ie,*last_ie;
  CAOTAbsAddr *abss;
  CAOTHeapGlbl *heap_glbls;
};
class CStreamBlk
{
  CStreamBlk *next,*last;
  U8	*body;
};
class CCodeCtrl
{
  CCodeCtrl *coc_next;
  CCodeMisc *coc_next_misc,*coc_last_misc;
  CIntermediateCodeBase	coc_head;
};
#define LFSF_DOC	1
#define LFSF_DEFINE	2
class CLexFile
{
  CLexFile *next;
  U8	*buf,
	*buf_ptr;
  I64	line_num,flags;
  U8	*full_name,
	*line_start;
  CDoc	*doc;
  CDocEntry *cur_entry;
  I32	depth;
  U8	last_U16,pad[3];
};
class CAOTCtrl
{
  I64	rip; //Inst ptr
  CAsmArg arg1,arg2;
  CAOTBinBlk *bin;
  I64	num_bin_U8s,
	max_align_bits,org;
  CAsmUnresolvedRef *local_unresolved,*glbl_unresolved;
  CAOTAbsAddr *abss;
  CAOTHeapGlbl *heap_glbls;
  I64	lst_col,lst_last_rip;
  U8	*last_label,*lst_last_line;
  CLexFile *lst_last_lfn;
  I64	seg_size;
  Bool	lst;
};
#define TK_EOF		0
#define TK_SUPERSCRIPT	0x001
#define TK_SUBSCRIPT	0x002
#define TK_NORMALSCRIPT	0x003
#define TK_IDENT	0x100
#define TK_STR		0x101
#define TK_I64		0x102
#define TK_CHAR_CONST	0x103
#define TK_F64		0x104
#define TK_PLUS_PLUS	0x105
#define TK_MINUS_MINUS	0x106
#define TK_DEREFERENCE	0x107
#define TK_DBL_COLON	0x108
#define TK_SHL		0x109
#define TK_SHR		0x10A
#define TK_EQU_EQU	0x10B
#define TK_NOT_EQU	0x10C
#define TK_LESS_EQU	0x10D
#define TK_GREATER_EQU	0x10E
#define TK_AND_AND	0x10F
#define TK_OR_OR	0x110
#define TK_XOR_XOR	0x111
#define TK_SHL_EQU	0x112
#define TK_SHR_EQU	0x113
#define TK_MUL_EQU	0x114
#define TK_DIV_EQU	0x115
#define TK_AND_EQU	0x116
#define TK_OR_EQU	0x117
#define TK_XOR_EQU	0x118
#define TK_ADD_EQU	0x119
#define TK_SUB_EQU	0x11A
#define TK_IF		0x11B
#define TK_IFDEF	0x11C
#define TK_IFNDEF	0x11D
#define TK_IFAOT	0x11E
#define TK_IFJIT	0x11F
#define TK_ENDIF	0x120
#define TK_ELSE		0x121
#define TK_MOD_EQU	0x122
#define TK_DOT_DOT	0x123
#define TK_ELLIPSIS	0x124
#define TK_INS_BIN	0x125
#define TK_INS_BIN_SIZE 0x126
#define TK_TKS_NUM	0x127
class CLexHashTableContext
{
  CLexHashTableContext *next;
  I64	old_flags,hash_mask;
  CHashFun *local_var_lst,
	*fun;
  CHashTable *hash_table_lst,
	*define_hash_table,
	*local_hash_table,
	*glbl_hash_table;
};
#define CCF_PMT			0x002
#define CCF_QUESTION_HELP	0x004
#define CCF_DONT_FREE_BUF	0x008
#define CCF_NO_DEFINES		0x010
#define CCF_IN_IF		0x020
#define CCF_KEEP_NEW_LINES	0x080
#define CCF_KEEP_DOT		0x100
#define CCF_KEEP_SIGN_NUM	0x200
#define CCF_KEEP_AT_SIGN	0x400
#define CCF_NO_CHAR_CONST	0x800
#define CCf_PASS_TRACE_PRESENT	12
#define CCF_NOT_CONST		0x0000020000
#define CCF_NO_REG_OPT		0x0000040000
#define CCF_IN_QUOTES		0x0000080000
#define CCF_EXE_BLK		0x0000100000
#define CCF_HAS_MISC_DATA	0x0000200000
#define CCF_HAS_RETURN		0x0000400000
#define CCF_ASM_EXPRESSIONS	0x0000800000
#define CCF_UNRESOLVED		0x0001000000
#define CCF_LOCAL		0x0002000000
#define CCF_FUN_EXP		0x0004000000
#define CCf_FUN_EXP		26
#define CCF_POSTINC		0x0008000000
#define CCF_POSTDEC		0x0010000000
#define CCF_PREINC		0x0020000000
#define CCF_PREDEC		0x0040000000
#define CCF_ARRAY		0x0080000000
#define CCF_RAX			0x0100000000
#define CCF_USE_LAST_U16	0x0200000000
#define CCf_USE_LAST_U16	33
#define CCF_LAST_WAS_DOT	0x0400000000
#define CCF_AOT_COMPILE		0x0800000000
#define CCf_AOT_COMPILE		35
#define CCF_NO_ABSS		0x1000000000
#define CCF_PAREN		0x2000000000
#define CCf_PAREN		37
#define CCF_CLASS_DOL_OFFSET	0x4000000000
#define CCF_DONT_MAKE_RES	0x8000000000
public class CCmpCtrl
{
  CCmpCtrl *next,*last;
  I64	token,
	flags,
	cur_i64;
  F64	cur_f64;
  U8	*cur_str;
  I64	cur_str_len,
	class_dol_offset;
  U8	*dollar_buf;
  I64	dollar_cnt;
  U8	*cur_help_idx;
  I64	last_U16,
	min_line,max_line,last_line_num,
	lock_cnt;
  U32	*char_bmp_alpha_numeric;
  CLexHashTableContext htc;
  CHashGeneric *hash_entry;
  CAbsCntsI64 abs_cnts;
  CAsmUndefHash *asm_undef_hash;
  CMemberLst *local_var_entry;
  CCodeMisc *lb_leave;
  U8	*cur_buf_ptr;
  CLexFile *lex_include_stk,
	*lex_prs_stk,
	*fun_lex_file;
  CStreamBlk *next_stream_blk,*last_stream_blk;
  CAOT	*aot;

  I64	pass,opts,pass_trace,saved_pass_trace,
	error_cnt,warning_cnt;

  //For intermediate codes with multiple float ops (int<->float conversions)
  I64	cur_ic_float_op_num,last_ic_float_op_num;
  CIntermediateCode *last_float_op_ic;
  Bool	last_dont_pushable,last_dont_popable,last_float_op_pos,
	dont_push_float,pad[4];

  CCodeCtrl coc;
  CPrsStk *ps;
  CAOTCtrl *aotc;
  I64	aot_depth,pmt_line;
#assert !($$&7)
};
public class CCmpGlbls
{
  CHashTable *asm_hash;
  CHashClass *internal_types[RT_RTS_NUM];
  CIntermediateCode ic_nop;
  U32	*dual_U16_tokens1,*dual_U16_tokens2,*dual_U16_tokens3,
	*binary_ops;
  I64	num_reg_vars,num_non_ptr_vars,
	stk_tmps_mask,reg_vars_mask,non_ptr_vars_mask;
  U8	*to_reg_vars_map,*non_ptr_vars_map;
  I64	size_arg_mask[9],
	compiled_lines;
};
class CUAsmGlbls
{
  CInst **table_16_32,**table_64;
  I64	table_16_32_entries,
	table_64_entries,
	ins64_arg_mask,
	signed_arg_mask,
	mem_arg_mask;
};
class CExcept
{
  CExcept *next,*last;
  I64	hndlr_catch,hndlr_untry,
	rsp,rbp,rflags,rsi,rdi,r10,r11,r12,r13,r14,r15;
};
#define CH_CURSOR	0x05
#define CH_SHIFT_SPACE	0x1F
#define CH_SPACE	0x20
// --- from KernelB.HH ---
// --- from KernelC.HH ---
