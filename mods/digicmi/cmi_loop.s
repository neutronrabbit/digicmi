| digicmi: the CMI loop controls' hooks in the stock sample players (cmi_loop.c has the logic).
| The render's code is at the same addresses in OS 1.53 and 1.54.
|
| 0x40075258, voice 0, and 0x400759a6, voices 1-7 (the voice in d4): the players' reads of a ONESHOT
| voice's PLAY, straight after its window came back in d0:d1 (was: mvs.b 54(a3),d2 / mvs.b 160(a0),d2,
| then move.l d2,d3; 6 bytes, by jsr). digicmi_play(v, play, win) gives the PLAY to read and may change
| the window. Out: d0, d1 (the window), d2 = d3 = PLAY; every other register as it was.

        .text
        .balign 2

        .macro  play_hook v_reg, src
        mvs.b   \src, %d2                       | the voice's PLAY, as stock reads it
        move.l  %a0, -(%sp)
        move.l  %a1, -(%sp)
        move.l  %d1, -(%sp)                     | win[1]
        move.l  %d0, -(%sp)                     | win[0]
        move.l  %sp, %a0
        move.l  %a0, -(%sp)                     | win
        move.l  %d2, -(%sp)                     | play
        move.l  \v_reg, -(%sp)                  | v
        jsr     digicmi_play
        lea     12(%sp), %sp
        move.l  %d0, %d2
        move.l  (%sp)+, %d0                     | the window, perhaps changed
        move.l  (%sp)+, %d1
        movea.l (%sp)+, %a1
        movea.l (%sp)+, %a0
        move.l  %d2, %d3
        rts
        .endm

        .globl  digicmi_rd0, digicmi_rdv
digicmi_rd0:                                    | voice 0 (0x40075258)
        play_hook #0, 54(%a3)
digicmi_rdv:                                    | voices 1-7 (0x400759a6): a0 is read before it is kept
        play_hook %d4, 160(%a0)

| 0x400760ba: a later stage's read of each voice's PLAY word, for its loop flag (the voice in a4)
| (was: move.w 54(a6),d0 ; lea 52(a5),a5; 8 bytes: jsr + nop). Out: d0, a5 as they were; the rest kept.
        .globl  digicmi_stage_s
digicmi_stage_s:
        lea     52(%a5), %a5
        move.w  54(%fp), %d0                    | a6 (fp): the voice's parameters
        move.l  %a0, -(%sp)
        move.l  %a1, -(%sp)
        move.l  %d1, -(%sp)
        ext.l   %d0
        move.l  %d0, -(%sp)                     | word
        move.l  %a4, -(%sp)                     | v
        jsr     digicmi_stage
        addq.l  #8, %sp
        move.l  (%sp)+, %d1
        movea.l (%sp)+, %a1
        movea.l (%sp)+, %a0
        rts

| 0x40008476: the LED frame's last step, the rest of the LEDs and the send (was: lea -12(sp),sp ;
| movem.l d2/a2-a3,(sp); 8 bytes, by jmp). digicmi_leds(mgr) goes first: 8 TRACK POLY's blue keys.
        .globl  digicmi_leds_s
digicmi_leds_s:
        move.l  4(%sp), -(%sp)
        jsr     digicmi_leds
        addq.l  #4, %sp
        lea     -12(%sp), %sp
        movem.l %d2/%a2-%a3, (%sp)
        jmp     0x4000847e

| 0x400225ca: the machine setter's entry (was: lea -28(sp),sp ; movem.l d2-d5/a2-a4,(sp); 8 bytes, by jmp).
| digicmi_machine_ok(object, machine) == 0 refuses the change as the setter does (d0 = 0), else the setter
| runs as it was.
        .globl  digicmi_mset_s
digicmi_mset_s:
        move.l  8(%sp), -(%sp)                  | machine
        move.l  8(%sp), -(%sp)                  | object
        jsr     digicmi_machine_ok
        addq.l  #8, %sp
        tst.l   %d0
        bne.s   1f
        rts                                     | refused: d0 = 0
1:      lea     -28(%sp), %sp
        movem.l %d2-%d5/%a2-%a4, (%sp)
        jmp     0x400225d2
