from flask import Flask, render_template, request, jsonify, Response
import os
from werkzeug.utils import secure_filename
import uuid
import base64
import time
import gc

app = Flask(__name__)

UPLOAD_FOLDER = 'uploads'
ALLOWED_EXTENSIONS = {'png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp'}
MAX_FILE_SIZE = 32 * 1024 * 1024  # 32MB，支持高分辨率照片

app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
app.config['MAX_CONTENT_LENGTH'] = MAX_FILE_SIZE

# 确保上传目录存在
os.makedirs(UPLOAD_FOLDER, exist_ok=True)
print(f"Upload folder created at: {os.path.abspath(UPLOAD_FOLDER)}")

# 存储最新的画面数据
latest_frame = None
latest_frame_time = 0

# 存储ESP32-CAM的IP地址
esp32_cam_ip = None
esp32_cam_ip_time = 0
ESP32_IP_TIMEOUT = 60  # IP地址有效期60秒

# 内存管理
LAST_CLEANUP_TIME = time.time()
CLEANUP_INTERVAL = 30  # 每30秒清理一次内存

# 拍照状态管理
is_capturing = False
last_capture_time = 0
CAPTURE_TIMEOUT = 30  # 拍照超时时间（秒），高分辨率需要更长时间
last_captured_photo = None  # 存储最近拍摄的照片

# 闪光灯控制
flash_enabled = False  # 闪光灯状态

# 已保存照片列表
saved_photos = []

def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

def cleanup_memory():
    """定期清理内存"""
    global LAST_CLEANUP_TIME
    
    current_time = time.time()
    if current_time - LAST_CLEANUP_TIME > CLEANUP_INTERVAL:
        print("执行内存清理...")
        
        # 强制垃圾回收
        gc.collect()
        
        # 记录清理时间
        LAST_CLEANUP_TIME = current_time
        print("内存清理完成")

@app.route('/')
def index():
    print("访问根路径")
    return render_template('streaming_simple.html')

@app.route('/upload', methods=['POST'])
def upload_file():
    if 'file' not in request.files:
        return jsonify({'error': '没有选择文件'}), 400
    
    file = request.files['file']
    
    if file.filename == '':
        return jsonify({'error': '没有选择文件'}), 400
    
    if not allowed_file(file.filename):
        return jsonify({'error': '不支持的文件格式'}), 400
    
    original_filename = secure_filename(file.filename)
    file_extension = original_filename.rsplit('.', 1)[1].lower()
    new_filename = f"{uuid.uuid4().hex}.{file_extension}"
    
    filepath = os.path.join(app.config['UPLOAD_FOLDER'], new_filename)
    file.save(filepath)
    
    return jsonify({
        'success': True,
        'filename': new_filename,
        'original_filename': original_filename,
        'size': os.path.getsize(filepath)
    })

@app.route('/api/upload', methods=['POST'])
def api_upload():
    if 'image' not in request.files:
        return jsonify({'error': '没有图片数据'}), 400
    
    file = request.files['image']
    
    if file.filename == '':
        new_filename = f"esp32cam_{uuid.uuid4().hex}.jpg"
    else:
        original_filename = secure_filename(file.filename)
        file_extension = original_filename.rsplit('.', 1)[1].lower() if '.' in original_filename else 'jpg'
        new_filename = f"{uuid.uuid4().hex}.{file_extension}"
    
    filepath = os.path.join(app.config['UPLOAD_FOLDER'], new_filename)
    file.save(filepath)
    
    return jsonify({
        'success': True,
        'filename': new_filename,
        'size': os.path.getsize(filepath),
        'url': f'/uploads/{new_filename}'
    })

@app.route('/api/raw', methods=['POST'])
def api_raw_upload():
    global latest_frame, latest_frame_time, esp32_cam_ip, esp32_cam_ip_time, is_capturing, last_captured_photo
    
    if not request.data:
        return jsonify({'error': '没有接收到数据'}), 400
    
    # 自动记录ESP32-CAM的IP地址
    # 优先从HTTP头中获取ESP32-CAM主动报告的IP地址
    esp32_ip_from_header = request.headers.get('X-ESP32-IP')
    if esp32_ip_from_header:
        esp32_cam_ip = esp32_ip_from_header
        esp32_cam_ip_time = time.time()
        print(f"ESP32-CAM IP地址已更新: {esp32_cam_ip}")
    else:
        # 备选：从请求连接信息获取
        client_ip = request.remote_addr
        if client_ip and client_ip != '127.0.0.1':
            # 只有当没有其他IP来源时才使用远程地址
            # 避免使用服务器自己的IP
            esp32_cam_ip = client_ip
            esp32_cam_ip_time = time.time()
            print(f"ESP32-CAM IP地址已更新(备选): {esp32_cam_ip}")
    
    # 检查是否为高分辨率拍照帧
    resolution = request.headers.get('X-Resolution', 'preview')
    
    if resolution == 'high':
        # 只保存高分辨率拍照帧
        timestamp = time.strftime('%Y%m%d_%H%M%S')
        new_filename = f"esp32cam_{timestamp}_{uuid.uuid4().hex[:8]}.jpg"
        filepath = os.path.join(app.config['UPLOAD_FOLDER'], new_filename)
        
        try:
            # 直接保存原始照片（不进行AI增强）
            with open(filepath, 'wb') as f:
                f.write(request.data)
            
            # 更新最近拍摄的照片
            last_captured_photo = {
                'filename': new_filename,
                'url': f'/uploads/{new_filename}',
                'size': len(request.data),
                'timestamp': time.time(),
                'enhanced': False
            }
            
            # 拍照完成，重置状态
            is_capturing = False
            print(f"高分辨率照片保存成功: {new_filename}")
            
            return jsonify({
                'success': True,
                'filename': new_filename,
                'size': len(request.data),
                'url': f'/uploads/{new_filename}',
                'captured': True,
                'enhanced': False
            })
        except Exception as e:
            print(f"保存文件失败: {str(e)}")
            return jsonify({'error': f'保存文件失败: {str(e)}'}), 500
    else:
        # 预览帧只更新内存，不保存到磁盘
        # 只有在非拍照状态下才更新预览帧
        if not is_capturing:
            # 验证数据完整性
            if len(request.data) > 0:
                # 更新最新画面
                latest_frame = request.data
                latest_frame_time = time.time()
            else:
                print('接收到空数据')
        
        return jsonify({
            'success': True,
            'message': '预览帧已更新',
            'size': len(request.data),
            'is_capturing': is_capturing
        })

@app.route('/api/frame', methods=['GET'])
def get_frame():
    global latest_frame, latest_frame_time
    
    try:
        if latest_frame:
            # 直接返回原始帧（不进行AI增强）
            return Response(latest_frame, mimetype='image/jpeg')
        else:
            # 返回默认图像
            default_image = os.path.join(app.static_folder, 'default.jpg') if hasattr(app, 'static_folder') else None
            if default_image and os.path.exists(default_image):
                with open(default_image, 'rb') as f:
                    return Response(f.read(), mimetype='image/jpeg')
            else:
                return jsonify({'error': '无画面数据'}), 404
    except Exception as e:
        print(f'获取帧失败: {str(e)}')
        # 返回默认图像作为备用
        default_image = os.path.join(app.static_folder, 'default.jpg') if hasattr(app, 'static_folder') else None
        if default_image and os.path.exists(default_image):
            with open(default_image, 'rb') as f:
                return Response(f.read(), mimetype='image/jpeg')
        else:
            return jsonify({'error': '获取帧失败'}), 500
    finally:
        # 定期清理内存
        cleanup_memory()

@app.route('/api/capture', methods=['POST'])
def capture():
    import requests
    global esp32_cam_ip, esp32_cam_ip_time, is_capturing, last_capture_time
    
    try:
        data = request.get_json() or {}
        esp32_ip = data.get('esp32_ip')
        
        # 验证IP地址
        if not esp32_ip:
            # 检查是否有缓存的IP地址
            if esp32_cam_ip and (time.time() - esp32_cam_ip_time) < ESP32_IP_TIMEOUT:
                esp32_ip = esp32_cam_ip
                print(f"使用缓存的ESP32-CAM IP地址: {esp32_ip}")
            else:
                return jsonify({'error': '未指定ESP32-CAM IP地址'}), 400
        
        # 验证IP地址格式
        import re
        ip_pattern = r'^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$'
        if not re.match(ip_pattern, esp32_ip):
            return jsonify({'error': '无效的IP地址格式'}), 400
        
        # 发送拍照指令
        is_capturing = True
        last_capture_time = time.time()
        
        print(f"发送拍照指令到: {esp32_ip}")
        
        # 发送POST请求到ESP32-CAM的/capture端点
        response = requests.post(
            f"http://{esp32_ip}/capture",
            timeout=10
        )
        
        if response.status_code == 200:
            print("拍照指令发送成功")
            return jsonify({
                'success': True,
                'message': '拍照指令已发送，正在处理中...',
                'esp32_ip': esp32_ip
            })
        else:
            print(f"ESP32-CAM响应异常: {response.status_code}")
            is_capturing = False
            return jsonify({
                'success': False,
                'error': f'ESP32-CAM响应异常: {response.status_code}'
            }), 500
            
    except requests.exceptions.RequestException as e:
        print(f"发送拍照指令失败: {str(e)}")
        is_capturing = False
        return jsonify({
            'success': False,
            'error': f'无法连接ESP32-CAM: {str(e)}'
        }), 500
    except Exception as e:
        print(f"拍照失败: {str(e)}")
        is_capturing = False
        return jsonify({
            'success': False,
            'error': f'拍照失败: {str(e)}'
        }), 500

@app.route('/api/capture/status', methods=['GET'])
def capture_status():
    global is_capturing, last_capture_time, last_captured_photo
    
    # 检查是否超时
    if is_capturing and (time.time() - last_capture_time) > CAPTURE_TIMEOUT:
        is_capturing = False
        print("拍照超时")
    
    return jsonify({
        'is_capturing': is_capturing,
        'last_captured_photo': last_captured_photo
    })

@app.route('/api/capture/clear', methods=['POST'])
def clear_capture():
    """清除最近拍摄的照片记录"""
    global last_captured_photo
    last_captured_photo = None
    return jsonify({'success': True, 'message': '已清除拍摄记录'})

# 闪光灯控制
@app.route('/api/flash', methods=['GET', 'POST'])
def flash_control():
    """闪光灯控制"""
    global flash_enabled, esp32_cam_ip
    
    if request.method == 'GET':
        # 获取当前闪光灯状态
        return jsonify({
            'success': True,
            'flash_enabled': flash_enabled
        })
    
    elif request.method == 'POST':
        # 设置闪光灯状态
        data = request.get_json() or {}
        enable = data.get('enable', False)
        
        # 发送指令到ESP32-CAM
        try:
            import requests
            esp32_ip = data.get('esp32_ip') or esp32_cam_ip or '192.168.137.74'
            
            # 发送闪光灯控制指令
            response = requests.post(
                f"http://{esp32_ip}/flash",
                json={'enable': enable},
                timeout=5
            )
            
            if response.status_code == 200:
                flash_enabled = enable
                print(f"闪光灯状态已更新: {'开启' if enable else '关闭'}")
                return jsonify({
                    'success': True,
                    'flash_enabled': flash_enabled,
                    'message': f"闪光灯已{'开启' if enable else '关闭'}"
                })
            else:
                return jsonify({
                    'success': False,
                    'error': 'ESP32-CAM响应异常'
                }), 500
                
        except Exception as e:
            print(f"闪光灯控制失败: {str(e)}")
            # 即使通信失败，也更新本地状态
            flash_enabled = enable
            return jsonify({
                'success': True,
                'flash_enabled': flash_enabled,
                'message': f"闪光灯状态已更新（本地）: {'开启' if enable else '关闭'}"
            })

# 相册功能
@app.route('/api/photos/save', methods=['POST'])
def save_photo():
    """保存照片到相册"""
    global saved_photos, last_captured_photo
    
    try:
        data = request.get_json() or {}
        filename = data.get('filename')
        
        if not filename and last_captured_photo:
            filename = last_captured_photo.get('filename')
        
        if not filename:
            return jsonify({'success': False, 'error': '没有指定照片'}), 400
        
        # 检查文件是否存在
        filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        if not os.path.exists(filepath):
            return jsonify({'success': False, 'error': '照片文件不存在'}), 404
        
        # 添加到已保存列表
        photo_info = {
            'filename': filename,
            'url': f'/uploads/{filename}',
            'saved_at': time.time(),
            'selected': False
        }
        
        # 避免重复添加
        existing = [p for p in saved_photos if p['filename'] == filename]
        if not existing:
            saved_photos.append(photo_info)
            print(f"照片已保存到相册: {filename}")
        
        return jsonify({
            'success': True,
            'message': '照片已保存到相册',
            'photo': photo_info
        })
        
    except Exception as e:
        print(f"保存照片失败: {str(e)}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/photos', methods=['GET'])
def get_photos():
    """获取相册中的所有照片"""
    return jsonify({
        'success': True,
        'photos': saved_photos
    })

@app.route('/api/photos/select', methods=['POST'])
def select_photo():
    """选择/取消选择照片"""
    global saved_photos
    
    try:
        data = request.get_json() or {}
        filename = data.get('filename')
        selected = data.get('selected', True)
        
        if not filename:
            return jsonify({'success': False, 'error': '没有指定照片'}), 400
        
        # 更新照片选择状态
        for photo in saved_photos:
            if photo['filename'] == filename:
                photo['selected'] = selected
                print(f"照片选择状态已更新: {filename} - {'已选择' if selected else '未选择'}")
                return jsonify({
                    'success': True,
                    'message': f"照片已{'选择' if selected else '取消选择'}",
                    'photo': photo
                })
        
        return jsonify({'success': False, 'error': '照片未找到'}), 404
        
    except Exception as e:
        print(f"选择照片失败: {str(e)}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/photos/delete', methods=['POST'])
def delete_photo():
    """删除照片"""
    global saved_photos
    
    try:
        data = request.get_json() or {}
        filename = data.get('filename')
        
        if not filename:
            return jsonify({'success': False, 'error': '没有指定照片'}), 400
        
        # 从列表中移除
        saved_photos = [p for p in saved_photos if p['filename'] != filename]
        
        # 删除文件
        filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        if os.path.exists(filepath):
            os.remove(filepath)
            print(f"照片已删除: {filename}")
        
        return jsonify({
            'success': True,
            'message': '照片已删除'
        })
        
    except Exception as e:
        print(f"删除照片失败: {str(e)}")
        return jsonify({'success': False, 'error': str(e)}), 500

# 静态文件服务
@app.route('/uploads/<filename>')
def uploaded_file(filename):
    from flask import send_from_directory
    return send_from_directory(app.config['UPLOAD_FOLDER'], filename)

if __name__ == '__main__':
    print("启动Flask服务器...")
    print(f"服务器将运行在 http://0.0.0.0:5000")
    print(f"访问地址: http://localhost:5000")
    app.run(host='0.0.0.0', port=5000, debug=False)
