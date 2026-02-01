#include "reg_loads.h"
#include "emitters.h"
#include "interop.h"

int compile_reg_load(struct code_block *block, uint8_t op)
{
    switch (op) {
    case 0x40: // ld b, b (nop)
        break;

    case 0x41: // ld b, c
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);  // D1 = C
        emit_swap(block, REG_68K_D_BC);  // B in low byte
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // B = C
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x42: // ld b, d
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);  // D1 = D
        emit_swap(block, REG_68K_D_DE);
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // B = D
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x43: // ld b, e
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_BC);  // B = E
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x44: // ld b, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // B = H
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x45: // ld b, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);  // L in low byte
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // B = L
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x46: // ld b, (hl)
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // result in D0
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_BC);  // D0 -> B
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x47: // ld b, a
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_BC);
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x48: // ld c, b
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);  // D1 = B
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // C = B
        break;

    case 0x49: // ld c, c (nop)
        break;

    case 0x4a: // ld c, d
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);  // D1 = D
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // C = D
        break;

    case 0x4b: // ld c, e
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_BC);  // C = E
        break;

    case 0x4c: // ld c, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // C = H
        break;

    case 0x4d: // ld c, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);  // L in low byte
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // C = L
        break;

    case 0x4e: // ld c, (hl)
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // result in D0
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_BC);  // D0 -> C
        break;

    case 0x4f: // ld c, a
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_BC);
        break;

    case 0x50: // ld d, b
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);  // D1 = B
        emit_swap(block, REG_68K_D_BC);
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);  // D = B
        emit_swap(block, REG_68K_D_DE);
        break;

    case 0x51: // ld d, c
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_DE);  // D = C
        emit_swap(block, REG_68K_D_DE);
        break;

    case 0x52: // ld d, d (nop)
        break;

    case 0x53: // ld d, e
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);  // D1 = E
        emit_swap(block, REG_68K_D_DE);  // D in low byte
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);  // D = E
        emit_swap(block, REG_68K_D_DE);
        break;

    case 0x54: // ld d, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);  // D = H
        emit_swap(block, REG_68K_D_DE);
        break;

    case 0x55: // ld d, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);  // L in low byte
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);  // D = L
        emit_swap(block, REG_68K_D_DE);
        break;

    case 0x56: // ld d, (hl)
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // result in D0
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_DE);  // D0 -> D
        emit_swap(block, REG_68K_D_DE);
        break;

    case 0x57: // ld d, a
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_DE);
        emit_swap(block, REG_68K_D_DE);
        break;

    case 0x58: // ld e, b
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_DE);  // E = B
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x59: // ld e, c
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_DE);  // E = C
        break;

    case 0x5a: // ld e, d
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);  // D1 = D
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);  // E = D
        break;

    case 0x5b: // ld e, e (nop)
        break;

    case 0x5c: // ld e, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);  // E = H
        break;

    case 0x5d: // ld e, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);  // L in low byte
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);  // E = L
        break;

    case 0x5e: // ld e, (hl)
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // result in D0
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_DE);  // D0 -> E
        break;

    case 0x5f: // ld e, a
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_DE);
        break;

    case 0x60: // ld h, b
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte position
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);  // copy B to H position
        emit_swap(block, REG_68K_D_BC);
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x61: // ld h, c
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte position
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);  // copy C to H position
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x62: // ld h, d
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte position
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);  // copy D to H position
        emit_swap(block, REG_68K_D_DE);
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x63: // ld h, e
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte position
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);  // copy E to H position
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x64: // ld h, h (nop)
        break;

    case 0x65: // ld h, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);  // D2 = L
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte position
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);  // copy L to H position
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x66: // ld h, (hl)
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // result in D0
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte position
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);  // D0 -> H position
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x67: // ld h, a
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_1);
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x68: // ld l, b
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);  // copy B to L position
        emit_swap(block, REG_68K_D_BC);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x69: // ld l, c
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);  // copy C to L position
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x6a: // ld l, d
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);  // copy D to L position
        emit_swap(block, REG_68K_D_DE);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x6b: // ld l, e
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);  // copy E to L position
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x6c: // ld l, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);  // D2 = H
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);  // restore order
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);  // L = H
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x6d: // ld l, l (nop)
        break;

    case 0x6e: // ld l, (hl)
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // result in D0
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);  // D0 -> L position
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x6f: // ld l, a
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;

    case 0x70: // ld (hl), b
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_0);  // D0 = B
        emit_swap(block, REG_68K_D_BC);
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_write_d0(block);
        break;

    case 0x71: // ld (hl), c
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_0);  // D0 = C
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_write_d0(block);
        break;

    case 0x72: // ld (hl), d
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_0);  // D0 = D
        emit_swap(block, REG_68K_D_DE);
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_write_d0(block);
        break;

    case 0x73: // ld (hl), e
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_0);  // D0 = E
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_write_d0(block);
        break;

    case 0x74: // ld (hl), h
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H in low byte
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);  // D0 = H
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);  // restore HL for address
        compile_call_dmg_write_d0(block);
        break;

    case 0x75: // ld (hl), l
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);  // D0 = L
        compile_call_dmg_write_d0(block);
        break;

    // 0x76 is HALT - not handled here

    case 0x77: // ld (hl), a
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_write_a(block);
        break;

    case 0x78: // ld a, b
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        emit_swap(block, REG_68K_D_BC);
        break;

    case 0x79: // ld a, c
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        break;

    case 0x7a: // ld a, d
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        emit_swap(block, REG_68K_D_DE);
        break;

    case 0x7b: // ld a, e
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        break;

    case 0x7c: // ld a, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        break;

    case 0x7d: // ld a, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        break;

    case 0x7e: // ld a, (hl)
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read_a(block);
        break;

    case 0x7f: // ld a, a (nop)
        break;

    default:
        return 0;
    }

    return 1;
}
