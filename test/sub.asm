.MODEL TINY
;.DOSSEG     ; Make sure you are using dos segment CODE, DATA + STACK
.DATA
    BUF     DB  100 DUP (?)
.CODE
.STARTUP 

mov ax, 123
mov bx, 3
sub ax,bx 

mov ax,123
mov bx,-3
sub ax,bx

mov ax,-123
mov bx, 3
sub ax,bx

mov ax,-123
mov bx,-3
sub ax,bx

mov al,0
mov ah,4Ch
int 21h
END