import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
IOC = ROOT / "Can.ioc"
INTERRUPTS_C = ROOT / "Core" / "Src" / "stm32u5xx_it.c"
MAIN_C = ROOT / "Core" / "Src" / "main.c"
STARTUP = ROOT / "startup_stm32u5a9xx.s"
LINKER = ROOT / "STM32U5a9xx_FLASH.ld"
RTCONFIG = ROOT / "RT-Thread" / "rtconfig.h"
CUBEMX_CMAKE = ROOT / "cmake" / "stm32cubemx" / "CMakeLists.txt"
THIRDPARTY_CMAKE = ROOT / "cmake" / "thirdparty.cmake"
ULOG_PROVENANCE = ROOT / "ThirdParty" / "rtthread_ulog" / "UPSTREAM.md"


class RTThreadNanoULogPortTests(unittest.TestCase):
    def test_rtthread_owns_startup_and_core_interrupts(self):
        startup = STARTUP.read_text(encoding="utf-8")
        interrupts = INTERRUPTS_C.read_text(encoding="utf-8")
        ioc = IOC.read_text(encoding="utf-8")

        self.assertIn("bl\tentry", startup)
        for handler in ("HardFault_Handler", "PendSV_Handler", "SysTick_Handler"):
            self.assertNotIn(f"void {handler}(void)", interrupts)
        for key in (
            "NVIC.SavedHardFaultIrqHandlerGenerated=true",
            "NVIC.SavedPendsvIrqHandlerGenerated=true",
            "NVIC.SavedSvcallIrqHandlerGenerated=true",
            "NVIC.SavedSystickIrqHandlerGenerated=true",
        ):
            self.assertIn(key, ioc)

    def test_ulog_is_native_async_without_any_backend(self):
        config = RTCONFIG.read_text(encoding="utf-8")
        linker = LINKER.read_text(encoding="utf-8")
        cmake = THIRDPARTY_CMAKE.read_text(encoding="utf-8")
        provenance = ULOG_PROVENANCE.read_text(encoding="utf-8")

        for option in (
            "#define RT_USING_ULOG",
            "#define ULOG_USING_ASYNC_OUTPUT",
            "#define ULOG_ASYNC_OUTPUT_BY_THREAD",
        ):
            self.assertIn(option, config)
        self.assertIn("KEEP(*(SORT(.rti_fn*)))", linker)
        self.assertIn("ThirdParty/rtthread_ulog/ulog.c", cmake)
        self.assertNotIn("rtthread_ulog/backend/console_be.c", cmake)
        self.assertNotIn("rtthread_ulog/backend/file_be.c", cmake)
        self.assertIn("tag `v4.1.1`", provenance)

    def test_rng_restore_is_present_in_source_and_build(self):
        main = MAIN_C.read_text(encoding="utf-8")
        cubemx_cmake = CUBEMX_CMAKE.read_text(encoding="utf-8")

        self.assertIn("RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI48", main)
        self.assertIn("RCC_OscInitStruct.HSI48State = RCC_HSI48_ON", main)
        self.assertIn("SecurityAccess_EntropyInit", main)
        self.assertIn("stm32u5xx_hal_rng.c", cubemx_cmake)


if __name__ == "__main__":
    unittest.main()
