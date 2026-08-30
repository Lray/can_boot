#!/usr/bin/env python3
"""Restore generated peripheral boundaries after STM32CubeMX code generation."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def replace_or_verify(text: str, old: str, new: str, path: Path) -> str:
    """Replace a CubeMX default once, or accept an already repaired file."""
    if old in text:
        return text.replace(old, new)
    if new in text:
        return text
    raise ValueError(f"unexpected CubeMX layout in {path.relative_to(ROOT)}")


def remove_or_verify(text: str, value: str, path: Path) -> str:
    """Remove a generated declaration when it is still present."""
    if value in text:
        return text.replace(value, "")
    return text


def repair_header(path: Path, declaration: str = "") -> str:
    text = path.read_text(encoding="utf-8")
    text = remove_or_verify(text, '#include "main.h"\n\n', path)
    if declaration:
        text = remove_or_verify(text, declaration, path)
    return text.rstrip() + "\n"


def repair_fdcan_header(path: Path) -> str:
    text = repair_header(
        path,
        "extern FDCAN_HandleTypeDef hfdcan1;\n\n",
    )
    if '#include "stm32u5xx_hal.h"' not in text:
        marker = "/* USER CODE BEGIN Includes */\n"
        if marker not in text:
            raise ValueError(f"unexpected CubeMX layout in {path.relative_to(ROOT)}")
        text = text.replace(
            marker,
            marker + '#include "stm32u5xx_hal.h"\n',
            1,
        )

    declaration = (
        "/**\n"
        " * Returns the CubeMX-owned FDCAN1 HAL handle to the platform driver.\n"
        " *\n"
        " * @return Address of the FDCAN1 handle.\n"
        " * @pre MX_FDCAN1_Init() has completed.\n"
        " */\n"
        "FDCAN_HandleTypeDef *FDCAN_Port_GetHandle(void);\n"
    )
    if "FDCAN_Port_GetHandle" not in text:
        marker = "/* USER CODE BEGIN Prototypes */\n"
        if marker not in text:
            raise ValueError(f"unexpected CubeMX layout in {path.relative_to(ROOT)}")
        text = text.replace(marker, marker + declaration, 1)

    return text.rstrip() + "\n"


def repair_fdcan(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    if '#include "main.h"' not in text:
        text = text.replace(
            '#include "fdcan.h"\n',
            '#include "fdcan.h"\n#include "main.h"\n',
            1,
        )
    if "FDCAN_Port_GetHandle" not in text:
        accessor = (
            "/* USER CODE BEGIN 1 */\n"
            "FDCAN_HandleTypeDef *FDCAN_Port_GetHandle(void)\n"
            "{\n"
            "    return &hfdcan1;\n"
            "}\n"
        )
        marker = "/* USER CODE BEGIN 1 */\n"
        if marker not in text:
            raise ValueError(f"unexpected CubeMX layout in {path.relative_to(ROOT)}")
        text = text.replace(marker, accessor, 1)
    text = text.replace(
        "  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)\n"
        "  {\n"
        "    Error_Handler();\n"
        "  }",
        "    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)\n"
        "    {\n"
        "        Error_Handler();\n"
        "    }",
    )
    text = text.replace(
        "    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)\n"
        "    {\n"
        "      Error_Handler();\n"
        "    }",
        "    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)\n"
        "    {\n"
        "        Error_Handler();\n"
        "    }",
    )
    text = text.replace(
        "  /* USER CODE END FDCAN1_MspInit 1 */\n  }\n}",
        "  /* USER CODE END FDCAN1_MspInit 1 */\n    }\n}",
    )
    return text.replace(
        "  /* USER CODE END FDCAN1_MspDeInit 1 */\n  }\n}",
        "  /* USER CODE END FDCAN1_MspDeInit 1 */\n    }\n}",
    )


def repair_usart(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    if '#include "main.h"' not in text:
        text = text.replace(
            '#include "usart.h"\n',
            '#include "usart.h"\n#include "main.h"\n',
            1,
        )
    text = replace_or_verify(
        text,
        "UART_HandleTypeDef huart1;",
        "static UART_HandleTypeDef s_uart1;",
        path,
    )
    text = re.sub(r"\bhuart1\b", "s_uart1", text)
    return text


def repair_main(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    generated_uart_init = (
        "  MX_GPIO_Init();\n"
        "  MX_FDCAN1_Init();\n"
        "  MX_USART1_UART_Init();\n"
        "  /* USER CODE BEGIN 2 */"
    )
    deferred_uart_init = (
        "  MX_GPIO_Init();\n"
        "  MX_FDCAN1_Init();\n"
        "  /* USER CODE BEGIN 2 */"
    )
    return replace_or_verify(text, generated_uart_init, deferred_uart_init, path)


def repaired_files() -> dict[Path, str]:
    return {
        ROOT / "Core" / "Inc" / "fdcan.h": repair_fdcan_header(
            ROOT / "Core" / "Inc" / "fdcan.h",
        ),
        ROOT / "Core" / "Inc" / "gpio.h": repair_header(
            ROOT / "Core" / "Inc" / "gpio.h",
        ),
        ROOT / "Core" / "Inc" / "usart.h": repair_header(
            ROOT / "Core" / "Inc" / "usart.h",
            "extern UART_HandleTypeDef huart1;\n\n",
        ),
        ROOT / "Core" / "Src" / "fdcan.c": repair_fdcan(
            ROOT / "Core" / "Src" / "fdcan.c",
        ),
        ROOT / "Core" / "Src" / "main.c": repair_main(
            ROOT / "Core" / "Src" / "main.c",
        ),
        ROOT / "Core" / "Src" / "usart.c": repair_usart(
            ROOT / "Core" / "Src" / "usart.c",
        ),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail instead of writing when CubeMX output needs repair",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    changed = []

    for path, repaired in repaired_files().items():
        current = path.read_text(encoding="utf-8")
        if current != repaired:
            changed.append(path.relative_to(ROOT))
            if not args.check:
                path.write_text(repaired, encoding="utf-8")

    if changed:
        action = "would repair" if args.check else "repaired"
        for path in changed:
            print(f"{action}: {path}")
        return 1 if args.check else 0

    print("CubeMX peripheral boundaries are already repaired")
    return 0


if __name__ == "__main__":
    sys.exit(main())
