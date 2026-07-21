#include <iostream>

#include "common/macros.h"

int main() {
  std::cout << "=== yquant_server ===" << std::endl;
  std::cout << "量化服务器已启动" << std::endl;

  // 测试宏定义
  int x = -10;
  ASSERT(x > 0, "x < 0");
  if (LIKELY(x > 0)) {
    std::cout << "分支预测测试: x > 0" << std::endl;
  }

  return 0;
}
