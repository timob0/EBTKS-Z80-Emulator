; MODEM.COM -- tiny CP/M 2.2 polling terminal, no machine overlay needed.
; Intended for assembly with zmac or other Z80-compatible assembler.
; Z80 UART: 88h data (RBR/THR), 8Dh line status: bit0 RX ready,
; bit5 TX holding register ready. Uses BDOS 6 direct nonblocking console
; input and BDOS 2 output; never touches BIOS or the existing console UART.
;
; A ready-to-copy MODEMTERM.COM can be generated with
;   python3 tools/build_modemterm.py -o MODEM.COM
; The Python builder emits the same opcodes without requiring an assembler.
        ORG 0100H
START:  LD DE,HELLO
        LD C,9
        CALL 5                 ; Print $-terminated greeting
LOOP:   LD E,0FFH
        LD C,6
        CALL 5                 ; BDOS direct console poll, no echo
        OR A
        JP Z,CHECKRX
        CP 18H                 ; Ctrl+X -> exit to CP/M
        JP Z,QUIT
        LD B,A
TXWAIT: IN A,(08DH)          ; 8250 LSR
        AND 20H               ; THRE
        JP Z,TXWAIT
        LD A,B
        OUT (088H),A          ; Virtual modem
CHECKRX:IN A,(08DH)
        AND 1                 ; Data ready
        JP Z,LOOP
        IN A,(088H)
        LD E,A
        LD C,2
        CALL 5                 ; Print remote byte
        JP LOOP
QUIT:   RET
HELLO:  DB 13,10,'MODEMTERM - CP/M Wi-Fi modem at 88h',13,10
        DB 'Type AT or ATDTvert.synchro.net:23',13,10
        DB 'Ctrl+X exits; +++ (guard time) enters command mode.',13,10,'$'
        END START
