from PIL import Image, ImageDraw, ImageFont
from tkinter import Tk, Label, Entry, StringVar, Button, Frame, Canvas, filedialog, ttk, Checkbutton, IntVar
import os
import re
import uuid

################################ - 字符配置 - ################################
# 固定字符编码范围（英文标点符、常用汉字、中文标签符）
CHAR_RANGES = "32-126, 19968-40869, 65281,65292,65306,65307,65311,12289,12290,8212,12300,12301,8236"

################################ - Font format - ################################
# 保留原有注释，此处省略
################################################################################

# Example usage (defaults parameters)
font_size_init = 24
map_start_code_init = "19968"  # Default map start code
per_page_init = 200  # 每页默认显示字符数

font_path = ("arial.ttf")  # Replace with your TTF font path

# Variables to track panning
start_x = 0
start_y = 0

# 全局变量存储字体数据
global_font_data = None
global_font_name = ""
global_font_size = 0
last_used_font_size = font_size_init  # 记录最后一次使用的行高

# 记录画布缩放比例
canvas_scale_x = 1.0
canvas_scale_y = 1.0

def get_char_list():
    """获取要生成的字符列表（使用固定编码范围）"""
    char_ranges_str = CHAR_RANGES
    list_char = []
    for intervals in char_ranges_str.split(','):
        first = intervals.strip()
        if not first:
            continue
        # 检查是单个字符还是区间
        try:
            second = first.split('-')[1].strip()
        except IndexError:
            try:
                list_char.append(int(first))
            except ValueError:
                continue
        else:
            try:
                start = int(first.split('-')[0].strip())
                end = int(second)
                for char in range(start, end + 1):
                    list_char.append(char)
            except ValueError:
                continue
    # 去重并排序（避免重复字符）
    list_char = sorted(list(set(list_char)))
    return list_char

def find_bounding_box(image):
    pixels = image.load()
    width, height = image.size
    x_min, y_min = width, height
    x_max, y_max = 0, 0

    for y in range(height):
        for x in range(width):
            if pixels[x, y] >= 1:  # Looking for 'on' pixels
                x_min = min(x_min, x)
                y_min = min(y_min, y)
                x_max = max(x_max, x)
                y_max = max(y_max, y)

    if x_min > x_max or y_min > y_max:  # No target pixels found
        return None
    return (x_min, y_min, x_max+1, y_max+1)

def load_ttf_font(font_path, font_size):
    """加载TTF字体并生成字体数据（使用指定的行高），过滤缺失字符"""
    global last_used_font_size
    last_used_font_size = font_size  # 更新最后使用的行高

    enforce_font_size = enforce_font_size_bool.get()
    pil_font = ImageFont.truetype(font_path, font_size)

    font_name = ' '.join(pil_font.getname())
    font_data = []
    
    # 统计缺失字符
    missing_chars = []
    
    for char_code in get_char_list():
        char = chr(char_code)
        
        image = Image.new("1", (font_size * 2, font_size * 2), 0) # generate mono bmp, 0 = black color
        draw = ImageDraw.Draw(image)
        
        try:
            # 尝试绘制字符
            draw.text((1, 0), char, font=pil_font, fill=255)
        except Exception:
            # 绘制失败，说明字体不支持该字符
            missing_chars.append(f"U+{char_code:04X} '{char}'")
            continue
        
        bbox = find_bounding_box(image)  # Get bounding box
        
        # 检查字符是否为空（无像素）或者bbox为None
        if bbox is None: 
            # 这可能是空格或控制字符，但也可能是字体缺失该字符的显示
            # 检查字符是否应该是可见的（非空格）
            if char_code not in [32, 160, 5760, 8192, 8193, 8194, 8195, 8196, 8197, 8198, 
                                 8199, 8200, 8201, 8202, 8239, 8287, 12288]:  # 空格类字符
                missing_chars.append(f"U+{char_code:04X} '{char}' (空白)")
                continue
            else:
                # 这是正常的空格字符
                width, height = 0, 0
                offset_x, offset_y = 0, 0
        else:
            x0, y0, x1, y1 = bbox
            width, height = x1 - x0, y1 - y0
            offset_x, offset_y = x0, y0
            
            # 额外的检查：如果bbox尺寸异常小，可能是字体不支持
            if width <= 1 and height <= 1 and char_code not in [46, 183]:  # 排除点号
                missing_chars.append(f"U+{char_code:04X} '{char}' (极小)")
                continue
                
            if offset_x:
                offset_x -= 1

        try: # Get the real glyph width including padding on the right that the box will remove
            adv_w = int(draw.textlength(char, font=pil_font))
            adv_w = max(adv_w, width + offset_x)
        except:
            adv_w = width + offset_x

        # Shift or crop glyphs that would be drawn beyond font_size
        if enforce_font_size and offset_y + height > font_size:
            print(f"    font_size exceeded: {offset_y+height}")
            if font_size - height >= 0:
                offset_y = font_size - height
            else:
                offset_y = 0
                height = font_size

        # Extract bitmap data
        cropped_image = image.crop(bbox) if bbox else Image.new("1", (0,0), 0)
        bitmap = []
        row = 0
        i = 0
        for y in range(height):
            for x in range(width):
                if i == 8:
                    bitmap.append(row)
                    row = 0
                    i = 0
                pixel = 1 if cropped_image.getpixel((x, y)) else 0
                row = (row << 1) | pixel
                i += 1
        if i > 0:
            bitmap.append(row << (8 - i)) # fill with zero the remaining empty bits
        bitmap = bitmap[0:int((width * height + 7) / 8)]

        # Create glyph entry
        glyph_data = {
            "char_code": char_code,
            "ofs_y": int(offset_y),
            "box_w": int(width),
            "box_h": int(height),
            "ofs_x": int(offset_x),
            "adv_w": int(adv_w),
            "bitmap": bitmap,
        }
        font_data.append(glyph_data)

    # 输出缺失字符信息
    if missing_chars:
        print(f"\n⚠️  字体缺失以下 {len(missing_chars)} 个字符:")
        for i in range(0, len(missing_chars), 5):  # 每行显示5个
            print("    " + ", ".join(missing_chars[i:i+5]))
    
    # Calculate max height
    max_height = max(g["ofs_y"] + g["box_h"] for g in font_data) if font_data else font_size
    if max_height > font_size:
        min_ofs_y = min((g["ofs_y"] if g["box_h"] > 0 else 1000) for g in font_data) if font_data else 0
        for key, glyph in enumerate(font_data):
            offset = glyph["ofs_y"]
            if min_ofs_y > 0 and offset >= min_ofs_y:
                offset -= min_ofs_y
            if chr(glyph["char_code"]) in ["_", "|"] and offset + glyph["box_h"] > font_size and offset > 0:
                offset -= 1
            font_data[key]["ofs_y"] = offset

        max_height = max(g["ofs_y"] + g["box_h"] for g in font_data) if font_data else font_size

    print(f"\n✅ 成功生成 {len(font_data)} 个字符，字体大小: {font_size}, 最大高度: {max_height}")
    
    return (font_name, font_size, font_data)

def load_c_font(file_path):
    # Load the C font
    font_name = "Unknown"
    font_size = 0
    font_data = []

    with open(file_path, 'r', encoding='UTF-8') as file:
        text = file.read()
        text = re.sub(r'//.*?$|/\*.*?\*/', '', text, flags=re.S|re.MULTILINE)
        text = re.sub(r'[\n\r\t\s]+', ' ', text)
        if m := re.search(r'\.name\s*=\s*"(.+)",', text):
            font_name = m.group(1)
        if m := re.search(r'\.height\s*=\s*(\d+),', text):
            font_size = int(m.group(1))
        if m := re.search(r'\.data\s*=\s*\{(.+?)\}', text):
            hexdata = [int(h, base=16) for h in re.findall('0x[0-9A-Fa-f]{2}', text)]

    while len(hexdata):
        char_code = hexdata[0] | (hexdata[1] << 8)
        if not char_code:
            break
        ofs_y = hexdata[2]
        box_w = hexdata[3]
        box_h = hexdata[4]
        ofs_x = hexdata[5]
        adv_w = hexdata[6]
        bitmap_len = int((box_w * box_h + 7) / 8)
        bitmap = hexdata[7:7+bitmap_len] if bitmap_len > 0 else []

        glyph_data = {
            "char_code": char_code,
            "ofs_y": ofs_y,
            "box_w": box_w,
            "box_h": box_h,
            "ofs_x": ofs_x,
            "adv_w": adv_w,
            "bitmap": bitmap,
        }
        font_data.append(glyph_data)

        hexdata = hexdata[7 + len(bitmap):]

    global last_used_font_size
    last_used_font_size = font_size  # 更新最后使用的行高
    return (font_name, font_size, font_data)

def generate_font_data(preview_only=True):
    """
    生成字体数据（全局缓存）
    :param preview_only: True=仅预览（行高不变时用缓存），False=强制重新生成（行高修改时）
    """
    global global_font_data, global_font_name, global_font_size

    # 获取当前输入框的行高
    try:
        current_input_size = int(font_height_input.get())
    except ValueError:
        current_input_size = last_used_font_size
        font_height_input.set(current_input_size)

    # 判断是否需要重新生成字体数据
    need_regenerate = False
    if not preview_only:
        need_regenerate = True
    elif global_font_data is None:
        need_regenerate = True
    elif current_input_size != global_font_size:
        need_regenerate = True  # 行高修改，强制重新生成

    if need_regenerate:
        if font_path.endswith(".c"):
            font_name, font_size, font_data = load_c_font(font_path)
        else:
            font_name, font_size, font_data = load_ttf_font(font_path, current_input_size)

        global_font_data = font_data
        global_font_name = font_name
        global_font_size = font_size
    else:
        # 复用缓存数据时，不修改输入框的值（解决自动回跳问题）
        font_name = global_font_name
        font_size = global_font_size
        font_data = global_font_data

    window.title(f"Font preview: Chinese {global_font_size} (总计{len(global_font_data)}字符)")
    return (font_name, font_size, font_data)

def render_current_page():
    """渲染当前页的字符（支持行高修改）"""
    global canvas_scale_x, canvas_scale_y
    # 传入preview_only=False，强制检查行高是否修改
    font_name, font_size, font_data = generate_font_data(preview_only=False)
    if not font_data:
        return

    # 获取分页参数
    try:
        per_page = int(per_page_input.get())
        current_page = int(page_num_input.get())
    except ValueError:
        per_page = per_page_init
        per_page_input.set(per_page)
        current_page = 1
        page_num_input.set(current_page)

    # 计算分页范围
    total_chars = len(font_data)
    total_pages = max(1, (total_chars + per_page - 1) // per_page)
    current_page = max(1, min(current_page, total_pages))
    page_num_input.set(current_page)
    page_info_label.config(text=f"第{current_page}/{total_pages}页 (总计{total_chars}字符)")

    # 计算当前页显示的字符范围
    start_idx = (current_page - 1) * per_page
    end_idx = min(start_idx + per_page, total_chars)
    current_font_data = font_data[start_idx:end_idx]

    max_height = max(font_size, max(g["ofs_y"] + g["box_h"] for g in font_data)) if font_data else font_size
    bounding_box = bounding_box_bool.get()

    # 清空画布
    canvas.delete("all")
    # 重置缩放比例记录
    canvas_scale_x = 1.0
    canvas_scale_y = 1.0
    # 重置滚动区域
    canvas.configure(scrollregion=(0, 0, canva_width*p_size, canva_height*p_size))

    offset_x_1 = 1
    offset_y_1 = 1

    for glyph_data in current_font_data:
        offset_y = glyph_data["ofs_y"]
        width = glyph_data["box_w"]
        height = glyph_data["box_h"]
        offset_x = glyph_data["ofs_x"]
        adv_w = glyph_data["adv_w"]

        # 换行逻辑
        if offset_x_1 + adv_w + 1 > canva_width:
            offset_x_1 = 1
            offset_y_1 += max_height + 1

        # 绘制字符位图
        byte_index = 0
        byte_value = 0
        bit_index = 0
        for y in range(height):
            for x in range(width):
                if bit_index == 0:
                    if byte_index < len(glyph_data["bitmap"]):
                        byte_value = glyph_data["bitmap"][byte_index]
                        byte_index += 1
                    else:
                        byte_value = 0
                if byte_value & (1 << (7 - bit_index)):
                    x1 = (x + offset_x_1 + offset_x) * p_size
                    y1 = (y + offset_y_1 + offset_y) * p_size
                    x2 = x1 + p_size
                    y2 = y1 + p_size
                    canvas.create_rectangle(x1, y1, x2, y2, fill="white")
                bit_index += 1
                bit_index %= 8

        # 绘制边界框
        if bounding_box:
            # 字符实际边界（红色）
            canvas.create_rectangle(
                (offset_x_1 + offset_x) * p_size,
                (offset_y_1 + offset_y) * p_size,
                (width + offset_x_1 + offset_x) * p_size,
                (height + offset_y_1 + offset_y) * p_size,
                width=1, outline="red", fill=''
            )
            # 字符占用区域（蓝色）
            canvas.create_rectangle(
                offset_x_1 * p_size,
                offset_y_1 * p_size,
                (offset_x_1 + adv_w) * p_size,
                (offset_y_1 + max_height) * p_size,
                width=1, outline='blue', fill=''
            )

        offset_x_1 += adv_w + 1

def change_page(delta):
    """切换页码（上一页/下一页）"""
    try:
        current_page = int(page_num_input.get())
    except ValueError:
        current_page = 1
    page_num_input.set(current_page + delta)
    render_current_page()

def save_font_data():
    """保存完整字体数据（不分页）"""
    font_name, font_size, font_data = generate_font_data(preview_only=False)
    if not font_data:
        return

    # 固定文件名前缀为 Chinese+行高
    fixed_filename = f"Chinese{font_size}"
    filename = filedialog.asksaveasfilename(
        title='Save Font',
        initialdir=os.getcwd(),
        initialfile=fixed_filename,
        defaultextension=".c",
        filetypes=(('Retro-Go Font', '*.c'), ('All files', '*.*')))

    if filename:
        with open(filename, 'w', encoding='UTF-8') as f:
            f.write(generate_c_font(font_name, font_size, font_data))

def generate_c_font(font_name, font_size, font_data):
    """生成C字体文件（固定命名为 Chinese+行高）"""
    # 固定变量名为 Chinese+行高
    fixed_name = f"Chinese{font_size}"
    max_height = max(font_size, max(g["ofs_y"] + g["box_h"] for g in font_data)) if font_data else font_size
    memory_usage = sum(len(g["bitmap"]) + 7 for g in font_data)  # 7 bytes for header

    # Calculate map data if enabled
    generate_map = generate_map_bool.get()
    map_start_code = int(map_start_code_input.get()) if generate_map and map_start_code_input.get().isdigit() else 0
    map_data = []
    if generate_map and font_data:
        char_codes = [g["char_code"] for g in font_data]
        max_char = max(char_codes)
        map_size = max_char - map_start_code + 1
        map_data = [0] * map_size  # Initialize with zeros
        data_index = 0
        for glyph in font_data:
            map_index = glyph["char_code"] - map_start_code
            if 0 <= map_index < map_size:
                map_data[map_index] = data_index
            data_index += 7 + len(glyph["bitmap"])  # 7 bytes header + bitmap size
        memory_usage += map_size * 4  # Each map entry is 4 bytes (uint32_t)

    file_data = "#include \"../rg_gui.h\"\n\n"
    file_data += "// File generated with font_converter.py (https://github.com/ducalex/retro-go/tree/dev/tools)\n\n"
    file_data += f"// Font           : {fixed_name}\n"
    file_data += f"// Point Size     : {font_size}\n"
    file_data += f"// Memory usage   : {memory_usage} bytes\n"
    file_data += f"// # characters   : {len(font_data)}\n"
    if generate_map and map_data:
        file_data += f"// Map start code : {map_start_code}\n"
        file_data += f"// Map size       : {len(map_data)} entries\n"
    file_data += "\n"

    font_type = 1
    if generate_map and map_data:
        # 固定映射表变量名：font_Chinese+行高_map
        file_data += f"static const uint32_t font_{fixed_name}_map[] = {{\n"
        for i in range(0, len(map_data), 8):
            line = map_data[i:i+8]
            file_data += "    " + ", ".join([f"0x{val:04X}" for val in line]) + ",\n"
        file_data += "};\n\n"
        font_type = 2

    # 固定字体结构体变量名：font_Chinese+行高
    file_data += f"const rg_font_t font_{fixed_name} = {{\n"
    # 固定name字段为 Chinese+行高
    file_data += f"    .name = \"{fixed_name}\",\n"
    file_data += f"    .type = {font_type},\n"
    file_data += f"    .width = 0,\n"
    file_data += f"    .height = {max_height},\n"
    file_data += f"    .chars = {len(font_data)},\n"
    if generate_map and map_data:
        file_data += f"    .map_start_code = {map_start_code},\n"
        file_data += f"    .map = font_{fixed_name}_map,\n"
        file_data += f"    .map_len = sizeof(font_{fixed_name}_map) / 4,\n"
    file_data += f"    .data = {{\n"
    for glyph in font_data:
        char_code = glyph['char_code']
        header_data = [char_code & 0xFF, char_code >> 8, glyph['ofs_y'], glyph['box_w'],
                       glyph['box_h'], glyph['ofs_x'], glyph['adv_w']]
        file_data += f"        /* U+{char_code:04X} '{chr(char_code)}' */\n        "
        file_data += ", ".join([f"0x{byte:02X}" for byte in header_data])
        file_data += f",\n        "
        if len(glyph["bitmap"]) > 0:
            file_data += ", ".join([f"0x{byte:02X}" for byte in glyph["bitmap"]])
            file_data += f","
        file_data += "\n"
    file_data += "\n"
    file_data += "        // Terminator\n"
    file_data += "        0x00, 0x00,\n"
    file_data += "    },\n"
    file_data += "};\n"

    return file_data

def select_file():
    """选择字体文件，清空缓存并重置行高"""
    global global_font_data
    global_font_data = None  # 清空缓存
    filename = filedialog.askopenfilename(
        title='Load Font',
        initialdir=os.getcwd(),
        filetypes=(('True Type Font', '*.ttf'), ('Retro-Go Font', '*.c'), ('All files', '*.*')))

    if filename:
        global font_path
        font_path = filename
        page_num_input.set(1)  # 重置页码
        render_current_page()

# 缩放函数（记录缩放比例）
def zoom(event):
    global canvas_scale_x, canvas_scale_y
    scale = 1.0
    if event.delta > 0:  # Scroll up to zoom in
        scale = 1.2
        canvas_scale_x *= 1.2
        canvas_scale_y *= 1.2
    elif event.delta < 0:  # Scroll down to zoom out
        scale = 0.8
        canvas_scale_x *= 0.8
        canvas_scale_y *= 0.8

    # 缩放画布内容
    canvas.scale("all", event.x, event.y, scale, scale)
    # 更新滚动区域
    canvas.configure(scrollregion=canvas.bbox("all"))

def start_pan(event):
    global start_x, start_y
    start_x = event.x
    start_y = event.y

def pan_canvas(event):
    global start_x, start_y
    dx = start_x - event.x
    dy = start_y - event.y
    canvas.move("all", -dx, -dy)
    start_x = event.x
    start_y = event.y

if __name__ == "__main__":
    window = Tk()
    window.title("Retro-Go Font Converter (Chinese Font)")

    # Get screen width and height
    screen_width = window.winfo_screenwidth()
    screen_height = window.winfo_screenheight()
    window.geometry(f"{screen_width}x{screen_height}")

    p_size = 8 # pixel size on the renderer
    canva_width = screen_width//p_size
    canva_height = screen_height//p_size-20  # 预留分页控件高度

    # ========== 顶部功能栏 ==========
    frame = Frame(window)
    frame.pack(anchor="center", padx=10, pady=2)

    # choose font button (file picker)
    choose_font_button = ttk.Button(frame, text='选择字体', command=select_file)
    choose_font_button.pack(side="left", padx=5)

    # Label and Entry for Font height
    Label(frame, text="行高").pack(side="left", padx=5)
    font_height_input = StringVar(value=str(font_size_init))
    Entry(frame, textvariable=font_height_input, width=4).pack(side="left", padx=5)

    # Variable to hold the state of the checkbox
    enforce_font_size_bool = IntVar(value=1)  # 0 for unchecked, 1 for checked
    Checkbutton(frame, text="强制行高", variable=enforce_font_size_bool).pack(side="left", padx=5)

    # 移除常用字模式开关

    # Label and Entry for Char ranges to include（保留编码范围输入框，使用固定范围）
    Label(frame, text="编码范围").pack(side="left", padx=5)
    list_char_ranges = StringVar(value=CHAR_RANGES)
    char_range_entry = Entry(frame, textvariable=list_char_ranges, width=30)
    char_range_entry.pack(side="left", padx=5)

    # Variable to hold the state of the checkbox
    bounding_box_bool = IntVar(value=1)  # 0 for unchecked, 1 for checked
    Checkbutton(frame, text="格子", variable=bounding_box_bool).pack(side="left", padx=10)

    # Variable to hold the state of the map generation checkbox
    generate_map_bool = IntVar(value=1)  # 0 for unchecked, 1 for checked
    Checkbutton(frame, text="生成映射表", variable=generate_map_bool).pack(side="left", padx=5)

    # Label and Entry for Map start code
    Label(frame, text="映射表起始码").pack(side="left", padx=5)
    map_start_code_input = StringVar(value=str(map_start_code_init))
    Entry(frame, textvariable=map_start_code_input, width=6).pack(side="left", padx=5)

    # Button to launch the font generation function
    preview_btn = Button(frame, text="预览", width=10, height=1, background="blue", foreground="white", command=render_current_page)
    preview_btn.pack(side="left", padx=5)

    # Button to launch the font exporting function
    save_btn = Button(frame, text="保存", width=10, height=1, background="blue", foreground="white", command=save_font_data)
    save_btn.pack(side="left", padx=5)

    # ========== 分页控件栏 ==========
    page_frame = Frame(window)
    page_frame.pack(anchor="center", padx=10, pady=2)

    # 每页显示数量
    Label(page_frame, text="每页字符数:").pack(side="left", padx=5)
    per_page_input = StringVar(value=str(per_page_init))
    per_page_entry = Entry(page_frame, textvariable=per_page_input, width=6)
    per_page_entry.pack(side="left", padx=5)

    # 页码切换按钮
    prev_btn = Button(page_frame, text="上一页", width=8, command=lambda: change_page(-1))
    prev_btn.pack(side="left", padx=5)

    # 当前页码
    Label(page_frame, text="页码:").pack(side="left", padx=5)
    page_num_input = StringVar(value="1")
    page_num_entry = Entry(page_frame, textvariable=page_num_input, width=6)
    page_num_entry.pack(side="left", padx=5)

    # 下一页按钮
    next_btn = Button(page_frame, text="下一页", width=8, command=lambda: change_page(1))
    next_btn.pack(side="left", padx=5)

    # 页码信息
    page_info_label = Label(page_frame, text="第1/1页 (总计0字符)")
    page_info_label.pack(side="left", padx=10)

    # ========== 画布区域 ==========
    canvas_frame = Frame(window)
    canvas_frame.pack(fill="both", expand=True, padx=2, pady=2)

    canvas = Canvas(canvas_frame, width=canva_width*p_size, height=canva_height*p_size, bg="black")
    canvas.configure(scrollregion=(0, 0, canva_width*p_size, canva_height*p_size))
    canvas.bind("<MouseWheel>", zoom)
    canvas.bind("<ButtonPress-1>", start_pan)  # Start panning
    canvas.bind("<B1-Motion>", pan_canvas)
    canvas.focus_set()
    canvas.pack(fill="both", expand=True)

    window.mainloop()