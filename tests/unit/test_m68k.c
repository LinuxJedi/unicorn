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
             {"test_divsl_int32_min_overflow", test_divsl_int32_min_overflow},
             {"test_divsw_int32_min_overflow", test_divsw_int32_min_overflow},
             {"test_divsll_int64_min_overflow", test_divsll_int64_min_overflow},
             {NULL, NULL}};
