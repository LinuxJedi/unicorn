#include "unicorn_test.h"

const uint64_t code_start = 0x1000;
const uint64_t code_len = 0x4000;

static void uc_common_setup(uc_engine **uc, uc_arch arch, uc_mode mode,
                            const char *code, uint64_t size,
                            uc_cpu_m68k cpu_model)
{
    OK(uc_open(arch, mode, uc));
    OK(uc_ctl_set_cpu_model(*uc, cpu_model));
    OK(uc_mem_map(*uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(*uc, code_start, code, size));
}

static void test_move_to_sr(void)
{

    uc_engine *uc;
    char code[] = "\x46\xfc\x27\x00"; // move    #$2700,sr
    int r_sr;

    uc_common_setup(&uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, code,
                    sizeof(code) - 1, UC_CPU_M68K_M68000);
    OK(uc_reg_read(uc, UC_M68K_REG_SR, &r_sr));

    r_sr = r_sr | 0x2000;

    OK(uc_reg_write(uc, UC_M68K_REG_SR, &r_sr));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_M68K_REG_SR, &r_sr));

    TEST_CHECK(r_sr == 0x2700);

    OK(uc_close(uc));
}

static void test_sr_contains_flags(void)
{
    uc_engine *uc;
    uint8_t code[] = {
        0x76, 0xed, // moveq #-19, %d3
    };

    uint32_t d3, sr;

    uc_common_setup(&uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, code, sizeof(code),
                    UC_CPU_M68K_M68000);

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));

    OK(uc_reg_read(uc, UC_M68K_REG_D3, &d3));
    OK(uc_reg_read(uc, UC_M68K_REG_SR, &sr));

    TEST_CHECK(d3 == 0xFFFFFFED);
    TEST_CHECK((sr & 0x8) /* N flag */ == 0x8);

    OK(uc_close(uc));
}

typedef struct InterruptInfo {
    uint32_t intno;
    bool called;
} InterruptInfo;

static void test_hook_interrupt(uc_engine *uc, uint32_t intno, void *data)
{
    InterruptInfo *info = data;

    info->called = true;
    info->intno = intno;
    OK(uc_emu_stop(uc));
}

static void test_move_from_sr_user_020_is_privileged(void)
{
    uc_engine *uc;
    uc_hook hook;
    uint8_t code[] = {
        0x40, 0xc0,                         // move sr,d0
    };
    uint32_t d0 = 0xdeadbeef;
    uint32_t sr = 0x0000;
    InterruptInfo info = {0};

    uc_common_setup(&uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, code, sizeof(code),
                    UC_CPU_M68K_M68020);
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_hook_interrupt, &info, 0, 0));

    OK(uc_reg_write(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_write(uc, UC_M68K_REG_SR, &sr));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));

    OK(uc_reg_read(uc, UC_M68K_REG_D0, &d0));

    if (!info.called || info.intno != 8 || d0 != 0xdeadbeef) {
        TEST_MSG("called=%u intno=%u d0=0x%08x", info.called, info.intno, d0);
    }
    TEST_CHECK(info.called);
    TEST_CHECK(info.intno == 8);
    TEST_CHECK(d0 == 0xdeadbeef);

    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
}

typedef struct ChkInterruptInfo {
    uint32_t expected_pc;
    uint32_t actual_pc;
    uint32_t intno;
    bool called;
} ChkInterruptInfo;

static void test_chk_hook_interrupt(uc_engine *uc, uint32_t intno, void *data)
{
    ChkInterruptInfo *info = data;

    info->called = true;
    info->intno = intno;
    OK(uc_reg_read(uc, UC_M68K_REG_PC, &info->actual_pc));
    OK(uc_emu_stop(uc));
}

static void check_chk_interrupt_info(const ChkInterruptInfo *info)
{
    if (!info->called || info->intno != 6 ||
        info->actual_pc != info->expected_pc) {
        TEST_MSG("called=%u intno=%u pc=0x%08x expected_pc=0x%08x",
                 info->called, info->intno, info->actual_pc,
                 info->expected_pc);
    }
    TEST_CHECK(info->called);
    TEST_CHECK(info->intno == 6);
    TEST_CHECK(info->actual_pc == info->expected_pc);
}

static void test_chkw_immediate_exception_reports_next_pc(void)
{
    uc_engine *uc;
    uc_hook hook;
    uint8_t code[] = {
        0x41, 0xbc, 0x00, 0x01,             // chk.w #1,d0
    };
    uint32_t d0 = 2;
    uint32_t sr = 0x2700;
    ChkInterruptInfo info = {
        .expected_pc = code_start + sizeof(code),
    };

    uc_common_setup(&uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, code, sizeof(code),
                    UC_CPU_M68K_M68000);
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_chk_hook_interrupt,
                   &info, 0, 0));

    OK(uc_reg_write(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_write(uc, UC_M68K_REG_SR, &sr));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));
    check_chk_interrupt_info(&info);

    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
}

static void test_chk2b_displacement_exception_reports_next_pc(void)
{
    uc_engine *uc;
    uc_hook hook;
    uint8_t code[] = {
        0x00, 0xe8,                         // chk2.b d16(a0),d0
        0x08, 0x00,                         // d0, chk2 extension word
        0x00, 0x00,                         // d16(a0)
    };
    uint8_t bounds[] = {
        0x00,                               // lower bound
        0x01,                               // upper bound
    };
    uint32_t d0 = 2;
    uint32_t a0 = code_start + 0x200;
    uint32_t sr = 0x2700;
    ChkInterruptInfo info = {
        .expected_pc = code_start + sizeof(code),
    };

    uc_common_setup(&uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, code, sizeof(code),
                    UC_CPU_M68K_M68020);
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_chk_hook_interrupt,
                   &info, 0, 0));
    OK(uc_mem_write(uc, a0, bounds, sizeof(bounds)));

    OK(uc_reg_write(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_write(uc, UC_M68K_REG_A0, &a0));
    OK(uc_reg_write(uc, UC_M68K_REG_SR, &sr));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));
    check_chk_interrupt_info(&info);

    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
}

static void test_divsl_int32_min_overflow(void)
{
    uc_engine *uc;
    uint8_t code[] = {
        0x4c, 0x7c, 0x08, 0x00,             // divs.l #$ffffffff,d0
        0xff, 0xff, 0xff, 0xff,
    };
    uint32_t d0 = 0x80000000;
    uint32_t sr = 0x2700;

    uc_common_setup(&uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, code, sizeof(code),
                    UC_CPU_M68K_M68020);

    OK(uc_reg_write(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_write(uc, UC_M68K_REG_SR, &sr));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));

    OK(uc_reg_read(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_read(uc, UC_M68K_REG_SR, &sr));

    TEST_CHECK(d0 == 0x80000000);
    TEST_CHECK((sr & 0x0002) == 0x0002);
    TEST_CHECK((sr & 0x0001) == 0);

    OK(uc_close(uc));
}

static void test_divsw_int32_min_overflow(void)
{
    uc_engine *uc;
    uint8_t code[] = {
        0x81, 0xfc, 0xff, 0xff,             // divs.w #$ffff,d0
    };
    uint32_t d0 = 0x80000000;
    uint32_t sr = 0x2700;

    uc_common_setup(&uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, code, sizeof(code),
                    UC_CPU_M68K_M68000);

    OK(uc_reg_write(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_write(uc, UC_M68K_REG_SR, &sr));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));

    OK(uc_reg_read(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_read(uc, UC_M68K_REG_SR, &sr));

    TEST_CHECK(d0 == 0x80000000);
    TEST_CHECK((sr & 0x0002) == 0x0002);
    TEST_CHECK((sr & 0x0001) == 0);

    OK(uc_close(uc));
}

static void test_divsll_int64_min_overflow(void)
{
    uc_engine *uc;
    uint8_t code[] = {
        0x4c, 0x7c, 0x0c, 0x01,             // divs.l #$ffffffff,d1:d0
        0xff, 0xff, 0xff, 0xff,
    };
    uint32_t d0 = 0x00000000;
    uint32_t d1 = 0x80000000;
    uint32_t sr = 0x2700;

    uc_common_setup(&uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, code, sizeof(code),
                    UC_CPU_M68K_M68020);

    OK(uc_reg_write(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_write(uc, UC_M68K_REG_D1, &d1));
    OK(uc_reg_write(uc, UC_M68K_REG_SR, &sr));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));

    OK(uc_reg_read(uc, UC_M68K_REG_D0, &d0));
    OK(uc_reg_read(uc, UC_M68K_REG_D1, &d1));
    OK(uc_reg_read(uc, UC_M68K_REG_SR, &sr));

    TEST_CHECK(d0 == 0x00000000);
    TEST_CHECK(d1 == 0x80000000);
    TEST_CHECK((sr & 0x0002) == 0x0002);
    TEST_CHECK((sr & 0x0001) == 0);

    OK(uc_close(uc));
}

TEST_LIST = {{"test_move_to_sr", test_move_to_sr},
             {"test_sr_contains_flags", test_sr_contains_flags},
             {"test_move_from_sr_user_020_is_privileged",
              test_move_from_sr_user_020_is_privileged},
             {"test_chkw_immediate_exception_reports_next_pc",
              test_chkw_immediate_exception_reports_next_pc},
             {"test_chk2b_displacement_exception_reports_next_pc",
              test_chk2b_displacement_exception_reports_next_pc},
             {"test_divsl_int32_min_overflow", test_divsl_int32_min_overflow},
             {"test_divsw_int32_min_overflow", test_divsw_int32_min_overflow},
             {"test_divsll_int64_min_overflow", test_divsll_int64_min_overflow},
             {NULL, NULL}};
