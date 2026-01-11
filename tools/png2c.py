from PIL import Image
import sys
import os

# ===================== 可自定义的默认配置 =====================
# DEFAULT_PNG_NAME = "hourglass.png"  # 你的PNG文件名（放脚本同目录）
# DEFAULT_OUTPUT_NAME = "image_hourglass.h"  # 生成的h文件名
# DEFAULT_VAR_NAME = "image_hourglass"        # C数组变量名
DEFAULT_PNG_NAME = "help.png"  # 你的PNG文件名（放脚本同目录）
DEFAULT_OUTPUT_NAME = "about_bg.h"  # 生成的h文件名
DEFAULT_VAR_NAME = "about_bg"        # C数组变量名
# 定义RGB565透明色：0xF81F 对应的RGB888值（关键！）
# 计算方式：0xF81F → R=31(255), G=1(4), B=31(255) → RGB888=(255, 4, 255)
TRANSPARENT_RGB888 = (255, 4, 255)  # 对应RGB565 0xF81F
# 透明像素填充值（可改：比如0xFFFF）
TRANSPARENT_RGB16 = 0x0000
# =============================================================

def png2c_rgb16(png_path, output_c_path, var_name="image_data"):
    # 1. 打开并处理图片（保留Alpha通道，不缩放，用原图尺寸）
    try:
        img = Image.open(png_path).convert("RGBA")  # 不缩放，保留原图尺寸
    except FileNotFoundError:
        print(f"❌ 错误：当前目录下找不到 {png_path} 文件！")
        print(f"   请确认图片文件放在：{os.path.dirname(os.path.abspath(__file__))}")
        input("按回车键退出...")
        sys.exit(1)
    
    # 直接使用图片原始尺寸（核心修改：不再强制缩放）
    width, height = img.size
    pixels = img.load()
    pixel_data = []

    # 2. 转换为RGB565格式（2字节/像素），处理透明色
    print(f"🔍 开始转换 {width}x{height} 原图（不缩放），透明色：{TRANSPARENT_RGB888}（对应RGB565 0xF81F）")
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            
            # 步骤1：识别RGB565 0xF81F对应的RGB888，标记为透明
            if (r, g, b) == TRANSPARENT_RGB888:
                a = 0  # 强制设为透明
            
            # 步骤2：保留黑色像素（RGB 0,0,0），不处理
            # 步骤3：透明像素填充为指定值（默认0x0000），非透明则正常转换
            if a == 0:
                rgb16 = TRANSPARENT_RGB16  # 透明像素填充值
            else:
                # 正常转换为RGB565（保留黑色：0,0,0 → 0x0000）
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                rgb16 = (r5 << 11) | (g6 << 5) | b5
            
            # 拆分高低字节（嵌入式RGB16格式，高字节在前）
            pixel_data.append((rgb16 >> 8) & 0xFF)
            pixel_data.append(rgb16 & 0xFF)

    # 3. 生成C语言数组（八进制转义，匹配示例格式）
    c_code = f"""#pragma once

/* 自动生成：{os.path.basename(png_path)} → {os.path.basename(output_c_path)} */
/* 尺寸：{width}x{height}（原图尺寸） | 格式：RGB565（2字节/像素） */
/* 透明色：RGB565 0xF81F（对应RGB888 {TRANSPARENT_RGB888}），黑色(0x0000)正常保留 */
static const struct {{
  unsigned int  width;
  unsigned int  height;
  unsigned int  bytes_per_pixel; /* 2:RGB16 */
  unsigned char pixel_data[{width * height * 2 + 1}];
}} {var_name} = {{
  {width}, {height}, 2,
  \""""
    
    # 拼接八进制转义的像素数据
    escape_str = ""
    for byte in pixel_data:
        escape_str += f"\\{oct(byte)[2:]}"  # 十进制→八进制转义（如255→\377）
    
    c_code += escape_str + "\",\n};\n"

    # 4. 写入输出文件
    with open(output_c_path, "w", encoding="utf-8") as f:
        f.write(c_code)
    
    print(f"✅ 转换成功！")
    print(f"   输入图片：{png_path}（尺寸：{width}x{height}）")
    print(f"   输出文件：{output_c_path}")
    print(f"   数组变量名：{var_name}")
    print(f"   透明色：RGB565 0xF81F（RGB888 {TRANSPARENT_RGB888}），黑色(0x0000)已保留")

if __name__ == "__main__":
    # 获取脚本所在目录（自动定位当前路径）
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # 拼接默认文件的完整路径
    default_png_path = os.path.join(script_dir, DEFAULT_PNG_NAME)
    default_output_path = os.path.join(script_dir, DEFAULT_OUTPUT_NAME)
    
    # 打印实际查找路径（调试用）
    print(f"🔍 脚本正在查找的图片路径：{default_png_path}")
    
    # 自动执行转换（完全使用原图尺寸，无缩放）
    png2c_rgb16(default_png_path, default_output_path, DEFAULT_VAR_NAME)