#!/usr/bin/env python3
import pathlib
import re
import unittest


SYSTEM_SOURCE = pathlib.Path(__file__).resolve().parents[1] / "Core" / "Src" / "system_stm32u5xx.c"


class SystemVectorTableTest(unittest.TestCase):
    def test_system_init_uses_linker_vector_symbol_for_vtor(self):
        source = SYSTEM_SOURCE.read_text(encoding="utf-8")

        self.assertIn("extern uint32_t g_pfnVectors;", source)
        self.assertRegex(source, r"SCB->VTOR\s*=\s*\(uint32_t\)&g_pfnVectors\s*;")
        self.assertIn("__DSB();", source)
        self.assertIn("__ISB();", source)

        system_init = re.search(r"void\s+SystemInit\s*\(void\)\s*\{(?P<body>.*?)\n\}", source, re.S)
        self.assertIsNotNone(system_init)
        self.assertNotIn("FLASH_BASE | VECT_TAB_OFFSET", system_init.group("body"))


if __name__ == "__main__":
    unittest.main()
