#!/usr/bin/env python3
import unittest
import json
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
LINKER_SCRIPT = ROOT / "STM32U5a9xx_FLASH.ld"
CMAKE_PRESETS = ROOT / "CMakePresets.json"
CAN_IOC = ROOT / "Can.ioc"
CUBEMX_REPAIR = ROOT / "scripts" / "repair_cubemx_generated_boundaries.py"
FDCAN_C = ROOT / "Core" / "Src" / "fdcan.c"
FDCAN_H = ROOT / "Core" / "Inc" / "fdcan.h"
CAN_DRIVER_C = ROOT / "transport" / "can_driver_stm32.c"
GPIO_C = ROOT / "Core" / "Src" / "gpio.c"
GPIO_H = ROOT / "Core" / "Inc" / "gpio.h"
MAIN_C = ROOT / "Core" / "Src" / "main.c"
CAN_NETWORK_H = ROOT.parent / "shared" / "can_network.h"
SYSCALLS_C = ROOT / "Core" / "Src" / "syscalls.c"
USART_C = ROOT / "Core" / "Src" / "usart.c"
USART_H = ROOT / "Core" / "Inc" / "usart.h"


class TargetRuntimeConstraintsTest(unittest.TestCase):
    def test_target_stack_budget_covers_cose_cwt_p256_verifier(self):
        linker_script = LINKER_SCRIPT.read_text(encoding="utf-8")
        match = re.search(
            r"_Min_Stack_Size\s*=\s*0x([0-9A-Fa-f]+)", linker_script
        )
        self.assertIsNotNone(match, "GNU linker stack size definition not found")
        stack_size = int(match.group(1), 16)

        self.assertGreaterEqual(
            stack_size,
            0x2000,
            "COSE/CWT P-256 verification must not run with the CubeMX 1KB default stack",
        )

    def test_initial_stack_pointer_stays_inside_sram(self):
        linker_script = LINKER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn(
            "_estack = ORIGIN(RAM) + LENGTH(RAM) - 8;",
            linker_script,
        )

    def test_fdcan_irq_uses_canopen_receive_callback_and_defers_uds(self):
        fdcan = FDCAN_C.read_text(encoding="utf-8")
        can_driver = CAN_DRIVER_C.read_text(encoding="utf-8")
        callback_match = re.search(
            r"void\s+HAL_FDCAN_RxFifo0Callback\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
            can_driver,
            re.DOTALL,
        )
        self.assertIsNotNone(callback_match, "FDCAN RX callback not found")

        callback_body = callback_match.group("body")
        self.assertIn("prv_read_can_received_msg", callback_body)
        self.assertNotIn("isotp_", can_driver)
        self.assertNotIn("UDS_", can_driver)

        self.assertIn("MX_FDCAN1_Init", fdcan)
        self.assertIn("HAL_FDCAN_Init", fdcan)
        self.assertIn("FDCAN_Port_GetHandle", fdcan)
        self.assertNotIn('"can_driver.h"', fdcan)
        self.assertNotIn("HAL_FDCAN_RxFifo0Callback", fdcan)
        self.assertNotIn("rt_mutex", fdcan)

        main = MAIN_C.read_text(encoding="utf-8")
        self.assertIn("static void OtaThreadEntry", main)
        self.assertIn("can_rx_buffer_init", main)
        self.assertIn("PollUdsCanFrames", main)
        self.assertIn("isotp_on_can_message", main)
        self.assertNotIn("s_can_module.CANerrorStatus |=", main)
        self.assertNotIn("s_can_module.CANtxCount", main)
        self.assertNotIn("s_can_rx_queue_error_count", main)
        self.assertNotIn("can_get_rx_count", main)
        self.assertNotIn("can_get_tx_count", main)
        self.assertNotIn("can_get_error_count", main)

    def test_runtime_separates_ota_and_can_log_workers(self):
        main = MAIN_C.read_text(encoding="utf-8")

        self.assertIn('"ota"', main)
        self.assertIn('"logtx"', main)
        self.assertIn('"heartbeat"', main)
        self.assertIn("static void OtaThreadEntry", main)
        self.assertIn("static void LogThreadEntry", main)
        self.assertIn("static void HeartbeatThreadEntry", main)
        self.assertIn("ULogCan_Poll", main)
        self.assertIn("SendHeartbeat", main)

        ota_start = main.index("static void OtaThreadEntry")
        log_start = main.index("static void LogThreadEntry")
        heartbeat_start = main.index("static void HeartbeatThreadEntry")
        ota_body = main[ota_start:log_start]
        log_body = main[log_start:]
        heartbeat_body = main[heartbeat_start:ota_start]
        self.assertIn("UDS_Poll", ota_body)
        self.assertIn("PollUdsCanFrames", ota_body)
        self.assertNotIn("CAN_ID_HEARTBEAT", ota_body)
        self.assertNotIn("ULogCan_Poll", ota_body)
        self.assertIn("ULogCan_Poll", log_body)
        self.assertIn("SendHeartbeat", heartbeat_body)
        self.assertIn("rt_thread_mdelay(CAN_HEARTBEAT_PERIOD_MS)", heartbeat_body)
        self.assertNotIn("UDS_Poll", heartbeat_body)

    def test_heartbeat_is_single_byte_node_liveness_state(self):
        main = MAIN_C.read_text(encoding="utf-8")
        network = CAN_NETWORK_H.read_text(encoding="utf-8")
        heartbeat_init = re.search(
            r"s_heartbeat_tx_buffer\s*=\s*can_tx_buffer_init\("
            r".*?CAN_ID_HEARTBEAT,\s*false,\s*(?P<dlc>\d+U)",
            main,
            re.DOTALL,
        )

        self.assertIsNotNone(heartbeat_init, "heartbeat TX buffer init not found")
        self.assertEqual(heartbeat_init.group("dlc"), "1U")
        self.assertIn("#define CAN_ID_HEARTBEAT 0x700U", network)
        self.assertIn("#define CAN_HEARTBEAT_STATE_ALIVE 0x05U", network)
        self.assertIn("#define CAN_HEARTBEAT_PERIOD_MS 1000U", network)
        self.assertIn("#define CAN_HEARTBEAT_TIMEOUT_MS 3000U", network)
        self.assertIn("not a complete CANopen NMT Heartbeat protocol", network)
        self.assertIn(
            "s_heartbeat_tx_buffer->data[0] = CAN_HEARTBEAT_STATE_ALIVE;",
            main,
        )
        self.assertNotIn("SendStartupCheckpoint", main)

    def test_can_error_processing_is_owned_by_main(self):
        main = MAIN_C.read_text(encoding="utf-8")
        can_driver = CAN_DRIVER_C.read_text(encoding="utf-8")
        can_header = (ROOT / "transport" / "can_driver.h").read_text(
            encoding="utf-8"
        )

        # Bus-off is left to FDCAN hardware auto-recovery; the main loop only
        # polls the PSR register and keeps the classified error status fresh.
        self.assertIn("can_module_process(&s_can_module)", main)
        self.assertIn("rt_thread_mdelay(1)", main)
        self.assertNotIn("g_can_recovery_sem", main)
        self.assertNotIn("rt_sem_take", main)
        self.assertNotIn("CAN_Recover", main)
        self.assertNotIn("rt_workqueue", main)
        self.assertNotIn("rt_thread_init(&s_can", main)

        self.assertNotIn("rt_sem_release(&g_can_recovery_sem)", can_driver)
        self.assertNotIn("HAL_FDCAN_ErrorStatusCallback", can_driver)
        self.assertNotIn("CAN_Recover", can_driver)
        self.assertNotIn("CAN_RecordErrorState", can_driver)
        self.assertNotIn("rt_thread_init", can_driver)
        self.assertNotIn("rt_thread_startup", can_driver)
        self.assertIn("CANerrorStatus", can_driver)
        self.assertIn("can_module_process", can_driver)
        self.assertIn("FDCAN_PSR_BO", can_driver)
        self.assertIn("CAN_ERRTX_BUS_OFF", can_driver)

        self.assertIn("void can_module_process(can_module_t* CANmodule)", can_header)
        self.assertIn("can_return_error_t", can_header)
        self.assertIn("CAN_ERRTX_BUS_OFF", can_header)
        self.assertNotIn("s_can_rx_count", can_driver)
        self.assertNotIn("s_can_tx_count", can_driver)
        self.assertNotIn("s_can_error_count", can_driver)
        self.assertNotIn("can_get_rx_count", can_header)
        self.assertNotIn("can_get_tx_count", can_header)
        self.assertNotIn("can_get_error_count", can_header)
        self.assertNotIn("CAN_Recover", can_header)
        self.assertNotIn("CAN_UpdateErrorStatus", can_header)
        self.assertNotIn("CAN_TakeErrorEvent", can_header)

    def test_fdcan_routes_fail_stop_through_main_error_handler(self):
        fdcan_header = FDCAN_H.read_text(encoding="utf-8")
        fdcan_source = FDCAN_C.read_text(encoding="utf-8")

        self.assertNotIn('"main.h"', fdcan_header)
        self.assertIn('"stm32u5xx_hal.h"', fdcan_header)
        self.assertIn("FDCAN_Port_GetHandle", fdcan_header)
        self.assertIn('"main.h"', fdcan_source)
        self.assertIn("Error_Handler", fdcan_source)
        self.assertIn("FDCAN_Port_GetHandle", fdcan_source)
        self.assertNotIn('"fatal_error.h"', fdcan_source)

    def test_generated_peripherals_do_not_depend_on_main(self):
        usart_header = USART_H.read_text(encoding="utf-8")
        usart_source = USART_C.read_text(encoding="utf-8")
        gpio_header = GPIO_H.read_text(encoding="utf-8")
        gpio_source = GPIO_C.read_text(encoding="utf-8")

        self.assertNotIn('"main.h"', usart_header)
        self.assertNotIn("extern UART_HandleTypeDef", usart_header)
        self.assertIn("static UART_HandleTypeDef s_uart1;", usart_source)
        self.assertIn("Error_Handler", usart_source)
        self.assertIn('"main.h"', usart_source)
        self.assertIn("int __io_putchar(int ch)", usart_source)
        self.assertIn("HAL_UART_Transmit(&s_uart1", usart_source)
        self.assertIn("return __io_putchar(ch);", usart_source)
        self.assertIn("__io_putchar(*ptr++);", SYSCALLS_C.read_text(encoding="utf-8"))

        self.assertNotIn('"main.h"', gpio_header)
        self.assertIn('"stm32u5xx_hal.h"', gpio_source)

    def test_app_console_uses_the_boot_console_uart_pins(self):
        ioc = CAN_IOC.read_text(encoding="utf-8")
        usart_source = USART_C.read_text(encoding="utf-8")

        self.assertIn("PA9.Signal=USART1_TX", ioc)
        self.assertIn("PA10.Signal=USART1_RX", ioc)
        self.assertNotIn("PB6.Signal=USART1_TX", ioc)
        self.assertNotIn("PB7.Signal=USART1_RX", ioc)
        self.assertIn("__HAL_RCC_GPIOA_CLK_ENABLE", usart_source)
        self.assertIn("HAL_GPIO_Init(GPIOA", usart_source)

    def test_cubemx_boundary_repair_is_idempotent(self):
        result = subprocess.run(
            [sys.executable, "-B", str(CUBEMX_REPAIR), "--check"],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_can_starts_before_flash_backed_startup_confirm(self):
        main = MAIN_C.read_text(encoding="utf-8")

        can_start = main.find("can_set_normal_mode(")
        confirm = main.find("ImageConfirm_RunStartupSelfCheck(startup_health_ok)")
        self.assertNotEqual(can_start, -1, "main must start FDCAN")
        self.assertNotEqual(confirm, -1, "main must run D12 startup confirm")
        self.assertIn(
            "startup_health_ok = startup_health_ok && can_started",
            main,
            "startup confirm must consume the actual CAN bring-up result",
        )

        self.assertLess(
            can_start,
            confirm,
            "FDCAN must come up before flash-backed D12 confirm so board probes can distinguish CAN bring-up from confirm/download failures",
        )
        heartbeat = main.find("    SendHeartbeat();", can_start)
        self.assertNotEqual(heartbeat, -1, "MCU must emit heartbeat after CAN bring-up")
        self.assertLess(
            heartbeat,
            confirm,
            "MCU must expose liveness before the flash-backed startup confirm",
        )

    def test_uart_debug_init_does_not_block_can_availability_probe(self):
        main = MAIN_C.read_text(encoding="utf-8")

        can_start = main.find("can_set_normal_mode(")
        uart_init = main.find("MX_USART1_UART_Init()")
        self.assertNotEqual(can_start, -1, "main must start FDCAN")
        self.assertNotEqual(uart_init, -1, "debug UART init must remain explicit")
        self.assertLess(
            can_start,
            uart_init,
            "debug UART init must not run before CAN availability checkpoint",
        )

    def test_cmake_post_build_exports_can_images(self):
        cmake_lists = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("CMAKE_OBJCOPY", cmake_lists)
        self.assertIn("-O binary", cmake_lists)
        self.assertIn("$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>", cmake_lists)
        self.assertIn("$<TARGET_FILE_BASE_NAME:${CMAKE_PROJECT_NAME}>", cmake_lists)
        self.assertIn("LINK_DEPENDS", cmake_lists)
        self.assertIn("CAN_FLASH_LINKER_SCRIPT", cmake_lists)

    def test_cmake_slot_presets_link_to_boot_payload_bases(self):
        presets = json.loads(CMAKE_PRESETS.read_text(encoding="utf-8"))
        cache_variables = {
            preset["name"]: preset["cacheVariables"]
            for preset in presets["configurePresets"]
            if "cacheVariables" in preset
        }

        self.assertEqual(
            cache_variables["Debug-slot0"]["CAN_FLASH_ORIGIN"],
            "0x08010200",
        )
        self.assertEqual(
            cache_variables["Debug-slot1"]["CAN_FLASH_ORIGIN"],
            "0x08030200",
        )
        self.assertEqual(
            cache_variables["Debug-slot0"]["CAN_FLASH_LENGTH"],
            "0x1FDC0",
        )
        self.assertEqual(
            cache_variables["Debug-slot1"]["CAN_FLASH_LENGTH"],
            "0x1FDC0",
        )


if __name__ == "__main__":
    unittest.main()
