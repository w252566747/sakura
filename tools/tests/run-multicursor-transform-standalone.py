#!/usr/bin/env python3
"""Compile and exercise production multi-cursor edit-coordinate transforms."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-multicursor-") as temp:
        temp_path = Path(temp)
        basis = temp_path / "basis"
        basis.mkdir()
        (basis / "SakuraBasis.h").write_text(r'''#pragma once
class CLogicInt {
public:
  CLogicInt(int value = 0) : value(value) {}
  operator int() const { return value; }
private:
  int value;
};
struct CLogicPoint { CLogicInt x; CLogicInt y; CLogicPoint(CLogicInt x, CLogicInt y) : x(x), y(y) {} };
inline int PointCompare(const CLogicPoint& a, const CLogicPoint& b) {
  if (a.y != b.y) return a.y < b.y ? -1 : 1;
  if (a.x != b.x) return a.x < b.x ? -1 : 1;
  return 0;
}
''', encoding="utf-8")
        (temp_path / "main.cpp").write_text(r'''#include "view/MultiCursorPointTransform.h"
#include <cassert>
#include <iostream>

static void Expect(const CLogicPoint& actual, int x, int y) {
  assert(static_cast<int>(actual.x) == x && static_cast<int>(actual.y) == y);
}

int main() {
  using multicursor::TransformPointAfterReplace;
  Expect(TransformPointAfterReplace({2, 1}, {4, 1}, {6, 1}, {5, 1}), 2, 1);
  Expect(TransformPointAfterReplace({9, 0}, {4, 0}, {4, 0}, {7, 0}), 12, 0);
  Expect(TransformPointAfterReplace({12, 2}, {5, 2}, {8, 2}, {6, 2}), 10, 2);
  Expect(TransformPointAfterReplace({5, 2}, {3, 2}, {8, 2}, {4, 2}), 4, 2);
  Expect(TransformPointAfterReplace({6, 8}, {2, 3}, {5, 6}, {1, 4}), 6, 6);
  Expect(TransformPointAfterReplace({15, 3}, {2, 1}, {8, 3}, {2, 5}), 9, 5);
  std::cout << "production multi-cursor coordinate transforms passed\n";
}
''', encoding="utf-8")
        executable = temp_path / "multicursor-test"
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
            "-I", str(temp_path), "-I", str(ROOT / "sakura_core"),
            str(temp_path / "main.cpp"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
