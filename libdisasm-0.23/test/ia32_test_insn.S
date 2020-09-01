#; register testing
push %eax
push %ebx
push %ecx
push %edx
push %esi
push %edi
push %esp
push %ebp
push %ax
push %bx
push %cx
push %dx
push %si
push %di
push %sp
push %bp
push $0x01
#; modr/m testing
pushw (%eax)
pushw (%ebx)
pushw (%ecx)
pushw (%edx)
pushw (%esi)
pushw (%edi)
pushw (%esp)
pushw (%ebp)
#; sib testing
pushl (%eax,%ebx,1) # pushl (%eax,%ebx)
pushl (%eax,%ebx,4)
pushl -0x16(%eax,%ebx,4)
pushl 0x10(,%ebx,4)
pushl 0x1000(,%ebx,4)
pushl 0x1000(%eax,%ebx,4)
pushl 0x1000(%eax)
push 0x80401000 # pushl 0x80401000
#; semi alphabetical insn list
aaa
aad $0x0A
aam $0x0A
aas
adc %eax, %ebx
adc $0x01, %ebx
adcl $0x01, (%ebx)
add %eax, %ebx
add $0x01, %ebx
addl $0x01, (%ebx)
addps %xmm0, %xmm1
and %eax, %ebx
and $0x01, %ebx
andnps %xmm0, %xmm1
andps %xmm0, %xmm1
arpl %ax, %bx
bound %eax, (%ecx)
bsf %eax, %ebx
bsr %eax, %ebx
bswap %ecx
bt %eax, %ebx
bt $0x04, %eax
btc %eax, %ebx
btc $0x04, %eax
btr %eax, %ebx
btr $0x04, %eax
bts %eax, %ebx
bts $0x04, %eax
lcall *0x80401000
call 0 # lcall 0x000000e6
lcall *(%eax)
cdq
clc
cld
cli
clts
cmc
cmova %eax, %ebx
cmovbe %eax, %ebx
cmovc %eax, %ebx
cmovg %eax, %ebx
cmovge %eax, %ebx
cmovl %eax, %ebx
cmovle %eax, %ebx
cmovnc %eax, %ebx
cmovno %eax, %ebx
cmovns %eax, %ebx
cmovnz %eax, %ebx
cmovo %eax, %ebx
cmovp %eax, %ebx
cmovnp %eax, %ebx
cmovs %eax, %ebx
cmovz %eax, %ebx
cmp %eax, %ebx
cmp $0x01, %ebx
cmpl (%ecx), %ebx
cmp %ecx, %ebx
cmpsb # cmpsb %ds:(%esi), %es:(%edi)
rep cmpsw # repz cmpsw %es:(%edi), %ds:(%esi)
repnz cmpsl # repnz cmpsl %es:(%edi), %ds:(%esi)
cmpxchg %eax, %ebx
cmpxchg8bq (%eax)
comiss %xmm0, %xmm1
cpuid
cwde
daa
das
dec %ebx
div %ecx
divps %xmm0, %xmm1
emms
enter $0x0004, $0x00
enter $0x0010, $0x10
fxrstor (%eax)
fxsave (%eax)
hlt
idivw (%ecx) # idivw (%ecx), %ax
idiv %ebx
imul %edx
imul $0x07, %ecx # imul $0x07, %ecx, %ecx
in $0x7F, %al
in %dx, %eax
in %dx, %al
inc %eax
inc %edx
inc %dx
inc %dh
nop
insl %dx, (%eax) # insl %edx, %es:(%edi)
insb # insb %dl, %es:(%edi)
insw # insw %dx, %es:(%edi)
int $0x01
int3
into
invd
invlpg (%eax)
iret
ja 0x0 # ja 0x0000017c
jbe 0x0 # jbe 0x00000182
jc 0x0 # jc 0x00000188
jcxz 0x0 # jcxz 0x0000018e
jg 0x0 # jg 0x00000191
jge 0x0 # jge 0x00000197
jl 0x0 # jl 0x0000019d
jle 0x0 # jle 0x000001a3
jmp 0x0 # ljmp 0x000001a8
jmp -1 # ljmp 0x000001ac
ljmp *0xE000FFFF
ljmp *0x80401000
ljmp *(%eax)
jnc 0x0 # jnc 0x000001c1
jno 0x0 # jno 0x000001c7
jns 0x0 # jns 0x000001cd
jnz 0x0 # jnz 0x000001d3
jo 0x0 # jo 0x000001d9
jpe 0x0 # jpe 0x000001df
jpo 0x0 # jpo 0x000001e5
js 0x0 # js 0x000001eb
jz 0x0 # jz 0x000001f1
jz -1 # jz 0x000001f6
jz 0xFFFF # jz 0x000101fc
jz 0xE000FFFF # jz 0xe0010202
jz 0x80401000 # jz 0x80401209
lahf
lar (%ebx), %ax
ldmxcsr (%eax)
ldsl (%edx), %eax
leal (%edx,%ecx,4), %eax
leave
lesl (%edx), %eax
lfence
lfsl (%edx), %eax
lgdt (%eax)
lgsl (%edx), %eax
lidt (%eax)
lldt (%eax)
lmsw (%eax)
lodsb # lodsb %ds:(%esi), %al
lodsl # lodsl %ds:(%esi), %eax
lodsw # lodsw %ds:(%esi), %ax
loop -1 # loop 0x00000236
loop 0 # loop 0x00000239
loopnz 127 # loopnz 0x000002ba
loopz -127 # loopz 0x000001be
lsl (%eax), %dx
lssl (%edx), %eax
ltr (%eax)
maskmovq %mm0, %mm1
maxps %xmm0, %xmm1
mfence
minps %xmm0, %xmm1
mov %eax, %ebx
mov $0x00000001, %eax
movl %eax, (%ebx)
movl %eax, 0x80401000
movw $0x0001, 0x80401000
mov %eax, %cr0
mov %eax, %dr0
movaps %xmm0, %xmm1
movd %eax, %mm0
movd %xmm0, %eax
movhps %xmm0, (%eax)
movlps (%eax), %xmm1
movmskps %xmm0, %eax
movntps %xmm0, (%eax)
movntq %mm0, (%eax) # movntq %xmm0, (%eax) # XFAIL
movsb # movsb %ds:(%esi), %es:(%edi)
repz movsl # repz movsl %ds:(%esi), %es:(%edi)
repnz movsw # repnz movsw %ds:(%esi), %es:(%edi)
movsx (%ebx), %ecx # movsxb (%ebx), %ecx # XFAIL
movups %xmm0, %xmm1
movzx (%ebx), %ecx # movzxb (%ebx), %ecx # XFAIL
mul %ebx
mulps %xmm0, %xmm1
neg %eax
negw -0x10(%ebp)
nop
not %eax
notw -0x10(%ebp)
or %ax, %bx
or %ecx, %edx
orl (%eax), %ebx
or $0x10, %eax
or $0x04, %al
orps %xmm0, %xmm1
out %al, $0x32
out %eax, %dx
outsw (%eax), %dx # outsw %ds:(%esi), %dx
outsb # outsb %ds:(%esi), %dl
rep outsw # repz outsw %ds:(%esi), %dx
repnz outsw # repnz outsw %ds:(%esi), %dx
packssdw %mm0, %mm1
packssdw %xmm0, %xmm1
packsswb %mm0, %mm1
packsswb %xmm0, %xmm1
packuswb %mm0, %mm1
packuswb %xmm0, %xmm1
paddb %mm0, %mm1
paddd %mm0, %mm1
paddsb %mm0, %mm1
paddsw %mm0, %mm1
paddusb %mm0, %mm1
paddusw %mm0, %mm1
paddw %mm0, %mm1
pand %mm0, %mm1
pandn %mm0, %mm1
pavgb %mm0, %mm1
pavgw %mm0, %mm1
pcmpeqb %mm0, %mm1
pcmpeqd %mm0, %mm1
pcmpeqw %mm0, %mm1
pcmpgtb %mm0, %mm1
pcmpgtd %mm0, %mm1
pcmpgtw %mm0, %mm1
pmaddwd %mm0, %mm1
pmaxsw %mm0, %mm1
pmaxub %mm0, %mm1
pminsw %mm0, %mm1
pminub %mm0, %mm1
pmovmskb %xmm0, %eax
pmulhuw %mm0, %mm1
pmulhw %mm0, %mm1
pmullw %mm0, %mm1
pop %eax
popl (%edx)
pop %fs
pop %ss
popa
popf
por %mm0, %mm1
prefetch (%edx)
prefetchnta (%ebp)
psadbw %mm0, %mm1
pslld %mm0, %mm1
pslldq $0x08, %xmm0
psllq %mm0, %mm1
psllw %mm0, %mm1
psrad %mm0, %mm1
psraw %mm0, %mm1
psrld %mm0, %mm1
psrldq $0x07, %xmm1
psrlq %mm0, %mm1
psrlw %mm0, %mm1
psubb %mm0, %mm1
psubd %mm0, %mm1
psubsb %mm0, %mm1
psubsw %mm0, %mm1
psubusb %mm0, %mm1
psubusw %mm0, %mm1
psubw %mm0, %mm1
punpckhbw %xmm0, %xmm1
punpckhdq %xmm0, %xmm1
punpckhwd %xmm0, %xmm1
punpcklbw %xmm0, %xmm1
punpckldq %xmm0, %xmm1
punpcklwd %xmm0, %xmm1
push %eax
push $0x16
pushl (%edx)
pushl 0x10(%ebp)
push %ss
push %gs
pusha
pushf
pxor %mm0, %mm1
rcl $0x00000001, %eax
rcl %cl, %ebx
rcl $0x07, %ecx
rcr $0x00000001, %eax
rcr %cl, %ebx
rcr $0x07, %ecx
rol $0x00000001, %eax
rol %cl, %ebx
rol $0x07, %ecx
ror $0x00000001, %eax
ror %cl, %ebx
ror $0x07, %ecx
rcpps %xmm0, %xmm1
rdmsr
rdpmc
rdtsc
ret
ret $0x0004
lret
rsm
rsqrtps %xmm0, %xmm1
sahf
shl $0x00000001, %eax
shl %cl, %ebx
shl $0x07, %ecx
sar $0x00000001, %eax
sar %cl, %ebx
sar $0x07, %ecx
shl $0x00000001, %eax
shl %cl, %ebx
shl $0x07, %ecx
shr $0x00000001, %eax
shr %cl, %ebx
shr $0x07, %ecx
sbb $0x02, %ah
sbb $0x03, %al
sbb $0x08, %ax
sbb $-0x08, %eax # sbb $0xf8, %eax # XFAIL
sbb $-0x09, %ax # sbb $0xf7, %ax # XFAIL
sbbl (%ecx), %eax
sbb %ecx, %eax
scasb # scasb %es:(%edi), %al
repz scasw # repz scasw %es:(%edi), %ax
repnz scasw # repnz scasw %es:(%edi), %ax
seta %al
setbe %al
setc %al
setg %al
setge %al
setl %al
setle %al
setnc %al
setno %al
setns %al
setnz %al
seto %al
setpe %al
setpo %al
sets %al
setz %al
sfence
sgdt (%eax)
shld %cl, %eax, %ebx
shld $0x07, %eax, %ebx
shrd %cl, %eax, %ebx
shrd $0x07, %eax, %ebx
shufps $0x08, %xmm0, %xmm1
sidt (%eax)
sldt (%eax)
smsw %ax
sqrtps %xmm0, %xmm1
stc
std
sti
stmxcsr (%eax)
stosb # stosb %al, %es:(%edi)
repz stosw # repz stosw %ax, %es:(%edi)
repnz stosw %ax, %es:(%edi)
str (%eax)
sub %ecx, %eax
sub $0x01, %eax
subl $0x01, (%eax)
subps %xmm0, %xmm1
sysenter
sysexit
test %eax, %eax
test %edx, %eax
testw $0x0001, (%eax)
ucomiss %xmm0, %xmm1
ud2
unpckhps %xmm0, %xmm1
unpcklps %xmm0, %xmm1
verr %ax
verw (%eax)
wait
wbinvd
wrmsr
xadd %eax, %ebx
xchg %eax, %eax # nop
xchg %ebx, %eax
xchgl %ebx, (%eax)
xlat
xor %al, %al
xor %ax, %ax
xor %eax, %eax
xor %ebx, %eax
xorl (%eax), %eax
xorps %xmm0, %xmm1
f2xm1
fabs
fadd (%eax)
fadd %ST(0), %ST(1)
faddp # faddp %st(0), %st(1) # XFAIL
fchs
wait
fnclex
fcmovb %ST(2), %ST(0)
fcmovbe %ST(2), %ST(0)
fcmove %ST(2), %ST(0)
fcmovnb %ST(2), %ST(0)
fcmovnbe %ST(2), %ST(0)
fcmovne %ST(2), %ST(0)
fcmovnu %ST(2), %ST(0)
fcmovu %ST(2), %ST(0)
fcom %ST(2) # fcom %st(2), %st(0) # XFAIL
fcom # fcom %st(1), %st(0) # XFAIL
fcomi %ST(2), %ST(0)
fcomip %ST(2), %ST(0)
fcomp # fcomp %st(1), %st(0) # XFAIL
fcompp
fcos
fdecstp
fdiv %ST(2), %ST(0)
fdivp # fdivrp %st(0), %st(1) # GNU as/objdump has fdivp/fdivrp swapped
fdivr %ST(2), %ST(0)
fdivrp # fdivp %st(0), %st(1) # GNU as/objdump has fdivp/fdivrp swapped
ffree %ST(2)
fincstp
wait
fninit
fld %ST(2) # fld %st(2), %st(0) # XFAIL
fld1
fldl2e
fldl2t
fldlg2
fldln2
fldpi
fldz
fmul %ST(2), %ST(0)
fmulp # fmulp %st(0), %st(1) # XFAIL
fnop
fpatan
fprem
fprem1
fptan
frndint
fscale
fsin
fsincos
fsqrt
fsub %ST(2), %ST(0)
fsubp # fsubrp %st(0), %st(1) # GNU as/objdump has fsubp/fsubrp swapped
fsubr %ST(0), %ST(1) # fsub %st(0), %st(1) # GNU as/objdump has fsub/fsubr swapped
fsubrp # fsubp %st(0), %st(1) # GNU as/objdump has fsubp/fsubrp swapped
ftst
fucom # fucom %st(0), %st(1) # XFAIL
fucom %ST(0) # fucom %st(0), %st(0) # XFAIL
fucomi %ST(1) # fucomi %st(1), %st(0) # XFAIL
fucomip %ST(2), %ST(0)
fucomp # fucomp %st(0), %st(1) # XFAIL
fucompp
fxam
fxch # fxch %st(1), %st(0) # XFAIL
fxch %ST(2) # fxch %st(2), %st(0) # XFAIL
fxtract
fyl2x
fyl2xp1
addpd %xmm0, %xmm1
addsd %xmm0, %xmm1
addss %xmm0, %xmm1
addsubpd %xmm0, %xmm1
addsubps %xmm0, %xmm1
andnpd %xmm0, %xmm1
andpd %xmm0, %xmm1
clflush (%eax)
cmppd $0x01, %xmm1, %xmm0
cmpps $0x01, %xmm1, %xmm0
cmpsd $0x01, %xmm1, %xmm0
cmpss $0x01, %xmm1, %xmm0
comisd %xmm1, %xmm0
cvtdq2pd %xmm0, %xmm1
cvtdq2ps %xmm0, %xmm1
cvtpd2dq %xmm0, %xmm1
cvtpd2pi %xmm0, %mm1
cvtpd2ps %xmm0, %xmm1
cvtpi2pd %mm0, %xmm1
cvtpi2ps %mm0, %xmm1
cvtps2dq %xmm0, %xmm1
cvtps2pd %xmm0, %xmm1
cvtps2pi %xmm0, %mm1
cvtsd2si %xmm0, %eax
cvtsd2ss %xmm0, %xmm1
cvtsi2sd %eax, %xmm1
cvtsi2ss %eax, %xmm1
cvtss2sd %xmm0, %xmm1
cvtss2si %xmm0, %eax
cvttpd2dq %xmm0, %xmm1
cvttpd2pi %xmm0, %mm1
cvttps2dq %xmm0, %xmm1
cvttps2pi %xmm0, %mm1
cvttsd2si %xmm0, %eax
cvttss2si %xmm0, %eax
divpd %xmm0, %xmm1
divsd %xmm0, %xmm1
divss %xmm0, %xmm1
femms
haddpd %xmm0, %xmm1
haddps %xmm0, %xmm1
hsubpd %xmm0, %xmm1
hsubps %xmm0, %xmm1
lddqu (%eax), %xmm0
maskmovdqu %xmm0, %xmm1
maxpd %xmm0, %xmm1
maxsd %xmm0, %xmm1
maxss %xmm0, %xmm1
minpd %xmm0, %xmm1
minsd %xmm0, %xmm1
minss %xmm0, %xmm1
monitor
movapd %xmm0, %xmm1
movddup %xmm0, %xmm1
movdqa %xmm0, %xmm1
movdqu %xmm0, %xmm1
movhlps %xmm0, %xmm1
movhpd (%eax), %xmm1
movlhps %xmm0, %xmm1
movlpd %xmm0, (%eax)
movmskpd %xmm1, %eax
movntdq %xmm0, (%eax)
movntil %ebx, (%eax)
movntpd %xmm0, (%ecx)
movq %xmm0, %xmm1
movq %mm0, %mm1
movq2dq %mm0, %xmm1
movsd %xmm0, %xmm1
movshdup %xmm0, %xmm1
movsldup %xmm0, %xmm1
movss %xmm0, %xmm1
movupd %xmm0, %xmm1
mulpd %xmm0, %xmm1
mulsd %xmm0, %xmm1
mulss %xmm0, %xmm1
mwait
orpd %xmm0, %xmm1
packssdw %mm0, %mm1
packsswb %mm0, %mm1
packuswb %mm0, %mm1
paddb %mm0, %mm1
paddd %mm0, %mm1
paddq %mm0, %mm1
paddsb %mm0, %mm1
paddsw %mm0, %mm1
paddusb %mm0, %mm1
paddusw %mm0, %mm1
paddw %mm0, %mm1
pand %mm0, %mm1
pandn %mm0, %mm1
pause
pavgb %mm0, %mm1
pavgusb %mm0, %mm1
pavgw %mm0, %mm1
pcmpeqb %mm0, %mm1
pcmpeqd %mm0, %mm1
pcmpeqw %mm0, %mm1
pcmpgtb %mm0, %mm1
pcmpgtd %mm0, %mm1
pcmpgtw %mm0, %mm1
pextrw $0x04, %mm0, %eax
pextrw $0x07, %xmm0, %edx
pfacc %mm0, %mm1
pfadd %mm0, %mm1
pfcmpeq %mm0, %mm1
pfcmpge %mm0, %mm1
pfcmpgt %mm0, %mm1
pfmax %mm0, %mm1
pfmin %mm0, %mm1
pfmul %mm0, %mm1
pfrcp %mm0, %mm1
pfrcpit1 %mm0, %mm1
pfrcpit2 %mm0, %mm1
pfrsqit1 %mm0, %mm1
pfrsqrt %mm0, %mm1
pfsub %mm0, %mm1
pfsubr %mm0, %mm1
pi2fd %mm0, %mm1
pinsrw $0x04, %eax, %mm0
pinsrw $0x07, (%eax), %xmm0
pmaddwd %mm0, %mm1
pmaxsw %mm0, %mm1
pmaxub %mm0, %mm1
pminsw %mm0, %mm1
pminub %mm0, %mm1
pmovmskb %mm0, %eax
pmulhrw %mm0, %mm1
pmulhuw %mm0, %mm1
pmulhw %mm0, %mm1
pmullw %mm0, %mm1
pmuludq %mm0, %mm1
por %mm0, %mm1
psadbw %mm0, %mm1
pshufd $0x07, %xmm0, %xmm1
pshufhw $0x01, %xmm0, %xmm1
pshuflw $0x02, %xmm0, %xmm1
pshufw $0x03, %mm0, %mm1
pslld %mm0, %mm1
psllq %mm0, %mm1
psllw %mm0, %mm1
psrad %mm0, %mm1
psraw %mm0, %mm1
psrld %mm0, %mm1
psrlq %mm0, %mm1
psrlw %mm0, %mm1
psubb %mm0, %mm1
psubd %mm0, %mm1
psubq %mm0, %mm1
psubsb %mm0, %mm1
psubsw %mm0, %mm1
psubusb %mm0, %mm1
psubusw %mm0, %mm1
psubw %mm0, %mm1
punpckhbw %mm0, %mm1
punpckhdq %mm0, %mm1
punpckhqdq %xmm0, %xmm1
punpckhwd %mm0, %mm1
punpcklbw %mm0, %mm1
punpckldq %mm0, %mm1
punpcklqdq %xmm0, %xmm1
punpcklwd %mm0, %mm1
pxor %mm0, %mm1
rcpss %xmm0, %xmm1
rsqrtss %xmm0, %xmm1
shufpd $0x07, %xmm0, %xmm1
sqrtpd %xmm0, %xmm1
sqrtsd %xmm0, %xmm1
sqrtss %xmm0, %xmm1
subpd %xmm0, %xmm1
subsd %xmm0, %xmm1
subss %xmm0, %xmm1
ucomisd %xmm0, %xmm1
unpckhpd %xmm0, %xmm1
unpcklpd %xmm0, %xmm1
xorpd %xmm0, %xmm1
fbld (%eax)
fbstp (%eax)
fcom (%eax)
fdiv (%eax)
fdivr (%eax)
fiadd (%eax)
ficom (%eax)
ficomp (%eax)
fidiv (%eax)
fidivr (%eax)
fild (%eax)
fimul (%eax)
fist (%eax)
fisttp (%eax)
fistp (%eax)
fisub (%eax)
fisubr (%eax)
fldcw (%eax)
fldenv (%eax)
fld (%eax)
fmul (%eax)
fnstsw (%eax)
frstor (%eax)
wait
fnsave (%eax)
fst (%eax)
wait
fnstcw (%eax)
wait
fnstenv (%eax)
fstp (%eax)
wait
fnstsw (%eax)
fsub (%eax)
fsubr (%eax)
movdq2q %xmm0, %mm1
nop
movdq2q %xmm2, %mm0
nop
pf2id %mm0, %mm1
nop
nop
nop
nop
#; prefix testing
