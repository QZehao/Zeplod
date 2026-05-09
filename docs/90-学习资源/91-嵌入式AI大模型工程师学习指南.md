# 嵌入式AI大模型工程师 - 保母级学习指南

> 适用人群：3年以上嵌入式开发经验者  
> 目标岗位：嵌入式AI工程师、边缘AI部署工程师、AI系统优化工程师  
> 学习周期：6-12个月（每天2-3小时）

---

## 目录

1. [学习路线总览](#1-学习路线总览)
2. [阶段一：AI基础速通（1-2个月）](#阶段一ai基础速通1-2个月)
3. [阶段二：嵌入式AI核心技能（2-3个月）](#阶段二嵌入式ai核心技能2-3个月)
4. [阶段三：部署实战与项目（2-3个月）](#阶段三部署实战与项目2-3个月)
5. [阶段四：进阶方向（持续学习）](#阶段四进阶方向持续学习)
6. [每日/每周学习计划](#每日每周学习计划)
7. [硬件准备清单](#硬件准备清单)

---

## 1. 学习路线总览

### 1.1 四阶段学习路径

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      嵌入式AI工程师学习路线图                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  阶段一（1-2月）         阶段二（2-3月）         阶段三（2-3月）    阶段四│
│  ┌──────────┐         ┌──────────┐         ┌──────────┐        ┌─────┐ │
│  │ AI基础   │         │ 模型压缩 │         │ 部署实战 │        │综合 │ │
│  │ 速通     │   →     │ 与量化   │   →     │ 与优化   │   →    │提升 │ │
│  └──────────┘         └──────────┘         └──────────┘        └─────┘ │
│       │                    │                    │                   │   │
│       ▼                    ▼                    ▼                   ▼   │
│  • PyTorch基础         • 量化技术          • llama.cpp         • 项目  │
│  • 神经网络核心       • 剪枝蒸馏          • TensorRT          • 作品集│
│  • Transformer         • 模型转换          • TFLite/ONNX       • 就业  │
│  • CNN基础            • 硬件加速          • NPU适配           • 深耕  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 核心技术栈

| 分类 | 技术点 | 优先级 | 难度 | 说明 |
|------|--------|--------|------|------|
| **编程语言** | Python | 必须 | 中 | 主要用于AI开发 |
| **编程语言** | C/C++ | 必须 | 高 | 嵌入式+推理优化 |
| **深度学习框架** | PyTorch | 必须 | 中 | 主流框架 |
| **模型压缩** | 量化(INT8/INT4) | 核心 | 高 | 嵌入式必备 |
| **模型压缩** | 剪枝、知识蒸馏 | 重要 | 高 | 进阶技术 |
| **部署框架** | ONNX Runtime | 核心 | 中 | 模型转换事实标准 |
| **部署框架** | TensorRT | 核心 | 高 | GPU推理优化 |
| **部署框架** | llama.cpp | 重要 | 中 | LLM边缘部署 |
| **边缘平台** | Jetson系列 | 必须 | 中 | NVIDIA生态 |
| **边缘平台** | RK3588 | 重要 | 中 | 国产NPU |
| **边缘平台** | ESP32 | 选学 | 中 | 极低成本场景 |

### 1.3 你的嵌入式优势

| 嵌入式经验 | 嵌入式AI价值 | 说明 |
|------------|--------------|------|
| 硬件资源约束优化 | ⭐⭐⭐⭐⭐ | 模型压缩核心技能 |
| 实时系统理解 | ⭐⭐⭐⭐⭐ | 低延迟推理优化 |
| 外设/驱动开发 | ⭐⭐⭐⭐ | NPU/GPU硬件加速 |
| 低功耗设计 | ⭐⭐⭐⭐ | 能效优化 |
| RTOS/Linux内核 | ⭐⭐⭐⭐ | 嵌入式系统部署 |

**核心观点：不要放弃嵌入式优势，要用AI能力武装它。**

---

## 阶段一：AI基础速通（1-2个月）

### 目标
- 理解神经网络核心原理
- 掌握PyTorch基本使用
- 能训练简单的图像分类模型
- 理解CNN、Transformer基础

### 周1：Python深度学习必备技能

#### 学习内容

| 天数 | 主题 | 内容 |
|------|------|------|
| Day 1-2 | Python基础 | 列表推导式、函数、类、模块 |
| Day 3-4 | NumPy核心 | 数组操作、广播机制、矩阵运算 |
| Day 5-6 | PyTorch张量 | 创建、运算、GPU加速 |
| Day 7 | 实战项目 | 实现矩阵乘法基准测试 |

#### 核心代码：PyTorch基础

```python
import torch
import numpy as np

# ===== 创建张量 =====
a = torch.tensor([1.0, 2.0, 3.0])        # 从列表创建
b = torch.zeros(3, 4)                    # 全零张量
c = torch.randn(100, 100)                # 随机张量
d = torch.arange(0, 10, 2)               # 范围张量

# ===== GPU加速 =====
if torch.cuda.is_available():
    x_gpu = x.cuda()                      # 移动到GPU
    y_gpu = y.cuda()
    z_gpu = torch.matmul(x_gpu, y_gpu)
    print(f"GPU: {z_gpu.device}")
else:
    print("使用CPU")

# ===== 自动求导（神经网络核心）=====
x = torch.tensor([1.0, 2.0, 3.0], requires_grad=True)
y = x ** 2
z = y.mean()
z.backward()                              # 自动计算梯度
print(x.grad)                              # 输出: tensor([0.6667, 1.3333, 2.0000])

# ===== 简单神经网络 =====
class SimpleNet(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.fc1 = torch.nn.Linear(784, 256)
        self.fc2 = torch.nn.Linear(256, 10)
        self.relu = torch.nn.ReLU()
    
    def forward(self, x):
        x = x.view(-1, 784)
        x = self.relu(self.fc1(x))
        x = self.fc2(x)
        return x

model = SimpleNet()
print(f"参数量: {sum(p.numel() for p in model.parameters())}")
```

---

### 周2：神经网络核心原理

#### 学习内容

| 天数 | 主题 | 内容 |
|------|------|------|
| Day 1-2 | 神经网络基础 | 感知机、多层网络、激活函数 |
| Day 3-4 | 前向传播与反向传播 | 链式法则、梯度计算 |
| Day 5-6 | 优化器与损失函数 | SGD、Adam、CrossEntropy、MSE |
| Day 7 | 实战：MNIST分类 | 用PyTorch实现MLP分类器 |

#### 核心代码：MNIST手写数字分类

```python
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader
from torchvision import datasets, transforms

# ===== 1. 数据加载 =====
transform = transforms.Compose([
    transforms.ToTensor(),                               # [0,1]
    transforms.Normalize((0.1307,), (0.3081,))          # MNIST标准化
])

train_dataset = datasets.MNIST('./data', train=True, download=True, transform=transform)
test_dataset = datasets.MNIST('./data', train=False, transform=transform)

train_loader = DataLoader(train_dataset, batch_size=64, shuffle=True)
test_loader = DataLoader(test_dataset, batch_size=1000)

# ===== 2. 定义神经网络 =====
class MNISTNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.flatten = nn.Flatten()
        self.fc = nn.Sequential(
            nn.Linear(784, 256),
            nn.ReLU(),
            nn.Linear(256, 128),
            nn.ReLU(),
            nn.Linear(128, 10)
        )
    
    def forward(self, x):
        x = self.flatten(x)
        return self.fc(x)

# ===== 3. 训练配置 =====
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
model = MNISTNet().to(device)
optimizer = optim.Adam(model.parameters(), lr=0.001)
criterion = nn.CrossEntropyLoss()

# ===== 4. 训练函数 =====
def train(epoch):
    model.train()
    total_loss = 0
    correct = 0
    for batch_idx, (data, target) in enumerate(train_loader):
        data, target = data.to(device), target.to(device)
        
        optimizer.zero_grad()
        output = model(data)
        loss = criterion(output, target)
        loss.backward()
        optimizer.step()
        
        total_loss += loss.item()
        pred = output.argmax(dim=1)
        correct += pred.eq(target).sum().item()
    
    print(f'Epoch {epoch}: Loss={total_loss/len(train_loader):.4f}, Acc={100*correct/len(train_dataset):.2f}%')

def test():
    model.eval()
    correct = 0
    with torch.no_grad():
        for data, target in test_loader:
            data, target = data.to(device), target.to(device)
            output = model(data)
            pred = output.argmax(dim=1)
            correct += pred.eq(target).sum().item()
    print(f'Test Acc: {100*correct/len(test_dataset):.2f}%')

# ===== 5. 训练循环 =====
for epoch in range(1, 6):
    train(epoch)
    test()

# ===== 6. 保存模型 =====
torch.save(model.state_dict(), 'mnist_mlp.pth')
print("模型已保存: mnist_mlp.pth")
```

---

### 周3：卷积神经网络（CNN）

#### 学习内容

| 天数 | 主题 | 内容 |
|------|------|------|
| Day 1-2 | 卷积层原理 | 卷积核、特征图、步长、填充 |
| Day 3-4 | 池化层与全连接层 | MaxPool、AvgPool、Flatten |
| Day 5-6 | CNN经典架构 | LeNet、AlexNet、VGG结构 |
| Day 7 | 实战：CIFAR-10 | 用CNN训练图像分类器 |

#### CNN结构图解

```
输入图像                    卷积层                    池化层
(H×W×3)              (特征图堆叠)              (尺寸减小)
  │                        │                        │
  ▼                        ▼                        ▼
┌─────────┐          ┌─────────────┐          ┌───────────┐
│  RGB    │   Conv   │  32个       │   Pool   │  32个     │
│  图像   │ ──────→ │  3×3滤波器  │ ──────→ │  特征图  │
│ 32×32×3 │          │  步长1     │          │  16×16   │
└─────────┘          └─────────────┘          └───────────┘
                                                 │
                                                 ▼
                              ┌─────────────────────────────┐
                              │       全连接层 + Softmax     │
                              │       输出10类概率           │
                              └─────────────────────────────┘
```

#### 核心代码：CIFAR-10分类

```python
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader
from torchvision import datasets, transforms

# ===== 数据增强 =====
train_transform = transforms.Compose([
    transforms.RandomCrop(32, padding=4),
    transforms.RandomHorizontalFlip(),
    transforms.ToTensor(),
    transforms.Normalize((0.4914, 0.4822, 0.4465), (0.2470, 0.2435, 0.2616))
])

test_transform = transforms.Compose([
    transforms.ToTensor(),
    transforms.Normalize((0.4914, 0.4822, 0.4465), (0.2470, 0.2435, 0.2616))
])

train_dataset = datasets.CIFAR10('./data', train=True, transform=train_transform, download=True)
test_dataset = datasets.CIFAR10('./data', train=False, transform=test_transform)

train_loader = DataLoader(train_dataset, batch_size=128, shuffle=True, num_workers=4)
test_loader = DataLoader(test_dataset, batch_size=256, num_workers=4)

# ===== CNN模型 =====
class SimpleCNN(nn.Module):
    def __init__(self):
        super().__init__()
        self.features = nn.Sequential(
            # Block 1: 3→32通道
            nn.Conv2d(3, 32, kernel_size=3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(inplace=True),
            nn.Conv2d(32, 32, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),                      # 32→16
            
            # Block 2: 32→64通道
            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.Conv2d(64, 64, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),                      # 16→8
            
            # Block 3: 64→128通道
            nn.Conv2d(64, 128, kernel_size=3, padding=1),
            nn.BatchNorm2d(128),
            nn.ReLU(inplace=True),
            nn.Conv2d(128, 128, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),                      # 8→4
        )
        
        self.classifier = nn.Sequential(
            nn.Dropout(0.5),
            nn.Linear(128 * 4 * 4, 256),
            nn.ReLU(inplace=True),
            nn.Dropout(0.5),
            nn.Linear(256, 10)
        )
    
    def forward(self, x):
        x = self.features(x)
        x = x.view(x.size(0), -1)
        x = self.classifier(x)
        return x

# ===== 训练 =====
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
model = SimpleCNN().to(device)
optimizer = optim.Adam(model.parameters(), lr=0.001)
criterion = nn.CrossEntropyLoss()
scheduler = optim.lr_scheduler.StepLR(optimizer, step_size=10, gamma=0.5)

def train_epoch(model, loader, criterion, optimizer, device):
    model.train()
    total_loss = 0
    correct = 0
    for data, target in loader:
        data, target = data.to(device), target.to(device)
        
        optimizer.zero_grad()
        output = model(data)
        loss = criterion(output, target)
        loss.backward()
        optimizer.step()
        
        total_loss += loss.item()
        pred = output.argmax(dim=1)
        correct += pred.eq(target).sum().item()
    
    return total_loss / len(loader), 100 * correct / len(loader.dataset)

def test(model, loader, criterion, device):
    model.eval()
    total_loss = 0
    correct = 0
    with torch.no_grad():
        for data, target in loader:
            data, target = data.to(device), target.to(device)
            output = model(data)
            loss = criterion(output, target)
            total_loss += loss.item()
            pred = output.argmax(dim=1)
            correct += pred.eq(target).sum().item()
    return total_loss / len(loader), 100 * correct / len(loader.dataset)

# ===== 训练循环 =====
for epoch in range(1, 21):
    train_loss, train_acc = train_epoch(model, train_loader, criterion, optimizer, device)
    test_loss, test_acc = test(model, test_loader, criterion, device)
    scheduler.step()
    
    print(f'Epoch {epoch:2d} | Train: {train_acc:.2f}% | Test: {test_acc:.2f}%')

# ===== 保存 =====
torch.save(model.state_dict(), 'cnn_cifar10.pth')
```

---

### 周4：Transformer与注意力机制

#### 学习内容

| 天数 | 主题 | 内容 |
|------|------|------|
| Day 1-2 | Attention机制 | 自注意力、多头注意力、QKV |
| Day 3-4 | Transformer架构 | 编码器、解码器、位置编码 |
| Day 5-6 | 预训练模型基础 | BERT、GPT系列、LLM概念 |
| Day 7 | 实战：文本分类 | 用Transformer提取特征 |

#### Attention机制图解

```
Self-Attention计算流程：

输入: X = [x1, x2, x3]

Step 1: 生成 Q, K, V
    Q = X · Wq
    K = X · Wk  
    V = X · Wv

Step 2: 计算注意力分数
    Score = Q · K^T / √dk

Step 3: Softmax归一化
    Attention = softmax(Score)

Step 4: 加权求和
    Output = Attention · V
```

#### 核心代码：自注意力机制

```python
import torch
import torch.nn as nn
import torch.nn.functional as F
import math

class SelfAttention(nn.Module):
    """缩放点积注意力"""
    def __init__(self, embed_size, heads=8):
        super().__init__()
        self.embed_size = embed_size
        self.heads = heads
        self.head_dim = embed_size // heads
        
        self.W_q = nn.Linear(embed_size, embed_size, bias=False)
        self.W_k = nn.Linear(embed_size, embed_size, bias=False)
        self.W_v = nn.Linear(embed_size, embed_size, bias=False)
        self.W_o = nn.Linear(embed_size, embed_size, bias=False)
    
    def forward(self, x, mask=None):
        batch, seq_len, _ = x.shape
        
        # 计算 Q, K, V
        Q = self.W_q(x).view(batch, seq_len, self.heads, self.head_dim).transpose(1, 2)
        K = self.W_k(x).view(batch, seq_len, self.heads, self.head_dim).transpose(1, 2)
        V = self.W_v(x).view(batch, seq_len, self.heads, self.head_dim).transpose(1, 2)
        
        # 注意力分数
        scores = torch.matmul(Q, K.transpose(-2, -1)) / math.sqrt(self.head_dim)
        
        if mask is not None:
            scores = scores.masked_fill(mask == 0, -1e9)
        
        attention = F.softmax(scores, dim=-1)
        out = torch.matmul(attention, V)
        
        out = out.transpose(1, 2).contiguous().view(batch, seq_len, self.embed_size)
        return self.W_o(out)

# ===== 测试 =====
x = torch.randn(2, 10, 64)  # batch=2, seq=10, embed=64
attention = SelfAttention(64, heads=8)
out = attention(x)
print(f"输入: {x.shape} → 输出: {out.shape}")
```

---

### 周5-8：阶段一小项目

#### 推荐项目

**项目1：表情识别系统**（推荐入门）
```
难度：★☆☆☆☆
时间：1周
技术：CNN + PyTorch + webcam
功能：实时从摄像头检测人脸并识别表情
学习收益：完整走一遍数据→训练→部署流程
```

**项目2：图像风格迁移**
```
难度：★★☆☆☆
时间：1周
技术：VGG19特征提取、损失网络
功能：将照片转换为特定艺术风格
学习收益：理解预训练模型复用
```

---

## 阶段二：嵌入式AI核心技能（2-3个月）

### 目标
- 掌握模型量化技术（INT8/INT4）
- 掌握模型剪枝与知识蒸馏
- 熟练使用ONNX、TensorRT等部署工具
- 能够在边缘设备上部署优化模型

---

### 周1-2：模型量化技术

#### 量化原理

```
FP32 (32位浮点)           INT8 (8位整数)
┌────────────────┐        ┌────────────┐
│  sign(1bit)    │        │            │
│  exp(8bit)     │  →     │  int8      │
│  frac(23bit)   │  压缩  │  (1byte)   │
└────────────────┘        └────────────┘

内存占用：32bits → 8bits (减少75%)
计算速度：3-4x 加速
精度损失：通常 < 2%
```

#### 量化类型对比

| 类型 | 实现难度 | 效果 | 精度损失 | 适用场景 |
|------|----------|------|----------|----------|
| 动态量化 | 低 | 中 | 低 | 快速验证 |
| 静态量化 | 中 | 高 | 中 | 生产部署 |
| 量化感知训练(QAT) | 高 | 最高 | 最低 | 追求精度 |

#### 核心代码：PyTorch量化

```python
import torch
import torch.nn as nn

# ===== 1. 动态量化（最简单，推荐入门）=====
# 适用于：Linear层、LSTM、Transformer
# 效果：减少75%内存，2-3x加速

model = SimpleCNN()  # 假设已有模型
model.load_state_dict(torch.load('cnn_cifar10.pth'))

# 动态量化 - 只量化Linear层
quantized_model = torch.quantization.quantize_dynamic(
    model,                                      # 模型
    {nn.Linear},                                # 要量化的层类型
    dtype=torch.qint8                           # 量化数据类型
)

# 推理测试
model.eval()
quantized_model.eval()

with torch.no_grad():
    x = torch.randn(1, 3, 32, 32)
    
    y_fp32 = model(x)
    y_int8 = quantized_model(x)
    
    print(f"FP32输出: {y_fp32.argmax(1).item()}")
    print(f"INT8输出: {y_int8.argmax(1).item()}")

# ===== 2. 静态量化（需要校准数据）=====
# 适用于：CNN、卷积层
# 效果：减少75%内存，3-4x加速

model_static = SimpleCNN()
model_static.load_state_dict(torch.load('cnn_cifar10.pth'))

# 设置量化配置
model_static.qconfig = torch.quantization.get_default_qconfig('fbgemm')
torch.quantization.prepare(model_static, inplace=True)

# 校准（使用训练数据的一部分）
print("校准中...")
model_static.eval()
with torch.no_grad():
    from torchvision import datasets
    train_dataset = datasets.CIFAR10('./data', train=True, transform=None)
    for i in range(50):  # 用50个样本校准
        data, _ = train_dataset[i]
        data = data.unsqueeze(0)
        model_static(data)

# 转换
torch.quantization.convert(model_static, inplace=True)
torch.save(model_static.state_dict(), 'cnn_static_quant.pth')

# ===== 3. INT4量化（使用bitsandbytes）=====
# 需要安装: pip install bitsandbytes

"""
from transformers import AutoModelForCausalLM, BitsAndBytesConfig

bnb_config = BitsAndBytesConfig(
    load_in_4bit=True,
    bnb_4bit_use_double_quant=True,
    bnb_4bit_quant_type="nf4",
    bnb_4bit_compute_dtype=torch.float16
)

model = AutoModelForCausalLM.from_pretrained(
    "TinyLlama/TinyLlama-1.1B-Chat-v1.0",
    quantization_config=bnb_config
)
"""
```

---

### 周3-4：模型剪枝与知识蒸馏

#### 剪枝代码

```python
import torch
import torch.nn as nn
import torch.nn.utils.prune as prune

# ===== 结构化剪枝示例 =====
class PrunableCNN(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(3, 64, 3, padding=1)
        self.conv2 = nn.Conv2d(64, 128, 3, padding=1)
        self.conv3 = nn.Conv2d(128, 256, 3, padding=1)
        self.fc1 = nn.Linear(256 * 4 * 4, 512)
        self.fc2 = nn.Linear(512, 10)
    
    def forward(self, x):
        x = torch.relu(self.conv1(x))
        x = torch.max_pool2d(torch.relu(self.conv2(x)), 2)
        x = torch.max_pool2d(torch.relu(self.conv3(x)), 2)
        x = x.view(x.size(0), -1)
        x = torch.relu(self.fc1(x))
        x = self.fc2(x)
        return x

model = PrunableCNN()

# ===== L1范数剪枝 =====
def prune_by_l1(amount=0.3):
    parameters_to_prune = [
        (model.conv1, 'weight'),
        (model.conv2, 'weight'),
        (model.conv3, 'weight'),
        (model.fc1, 'weight'),
        (model.fc2, 'weight'),
    ]
    
    prune.l1_unstructured(parameters_to_prune, amount=amount)
    
    # 查看稀疏度
    for name, module in model.named_modules():
        if hasattr(module, 'weight'):
            if module.weight is not None and hasattr(module.weight, 'grad_fn'):
                if module.weight.grad_fn is not None:
                    sparsity = 100 * float(torch.sum(module.weight == 0)) / float(module.weight.nelement())
                    print(f"{name} 稀疏度: {sparsity:.2f}%")

# ===== 永久化剪枝 =====
def make_pruning_permanent():
    for module in model.modules():
        if hasattr(module, 'weight') and hasattr(module, 'weight_orig'):
            prune.remove(module, 'weight')

# ===== 训练时迭代剪枝 =====
def iterative_pruning(model, train_loader, epochs=50, prune_amount=0.1):
    optimizer = torch.optim.Adam(model.parameters(), lr=0.001)
    criterion = nn.CrossEntropyLoss()
    
    for epoch in range(epochs):
        model.train()
        for data, target in train_loader:
            optimizer.zero_grad()
            output = model(data)
            loss = criterion(output, target)
            loss.backward()
            optimizer.step()
        
        # 每10个epoch剪枝一次
        if epoch % 10 == 0:
            parameters_to_prune = []
            for name, module in model.named_modules():
                if isinstance(module, (nn.Conv2d, nn.Linear)):
                    parameters_to_prune.append((module, 'weight'))
            
            prune.l1_unstructured(parameters_to_prune, amount=prune_amount)
            print(f"Epoch {epoch}: 剪枝完成 {prune_amount*100:.0f}%")
```

#### 知识蒸馏代码

```python
import torch
import torch.nn as nn
import torch.nn.functional as F

# ===== 知识蒸馏损失 =====
class DistillationLoss(nn.Module):
    def __init__(self, temperature=4.0, alpha=0.5):
        super().__init__()
        self.temperature = temperature
        self.alpha = alpha
    
    def forward(self, student_logits, teacher_logits, labels):
        # 硬标签损失
        hard_loss = F.cross_entropy(student_logits, labels)
        
        # 软标签损失
        soft_student = F.log_softmax(student_logits / self.temperature, dim=1)
        soft_teacher = F.softmax(teacher_logits / self.temperature, dim=1)
        soft_loss = F.kl_div(soft_student, soft_teacher, reduction='batchmean') * (self.temperature ** 2)
        
        return self.alpha * hard_loss + (1 - self.alpha) * soft_loss

# ===== 教师-学生模型 =====
class StudentModel(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Flatten(),
            nn.Linear(784, 256),
            nn.ReLU(),
            nn.Linear(256, 128),
            nn.ReLU(),
            nn.Linear(128, 10)
        )
    
    def forward(self, x):
        return self.net(x)

class TeacherModel(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Flatten(),
            nn.Linear(784, 512),
            nn.ReLU(),
            nn.Linear(512, 256),
            nn.ReLU(),
            nn.Linear(256, 128),
            nn.ReLU(),
            nn.Linear(128, 10)
        )
    
    def forward(self, x):
        return self.net(x)

# ===== 蒸馏训练 =====
def distillation_train(student, teacher, train_loader, epochs=20):
    student = student.cuda()
    teacher = teacher.cuda()
    teacher.eval()
    
    optimizer = torch.optim.Adam(student.parameters(), lr=0.001)
    criterion = DistillationLoss(temperature=4.0, alpha=0.5)
    
    for epoch in range(epochs):
        student.train()
        total_loss = 0
        
        for data, target in train_loader:
            data, target = data.cuda(), target.cuda()
            
            with torch.no_grad():
                teacher_logits = teacher(data)
            
            student_logits = student(data)
            loss = criterion(student_logits, teacher_logits, target)
            
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            
            total_loss += loss.item()
        
        print(f"Epoch {epoch+1}: Loss = {total_loss/len(train_loader):.4f}")
    
    return student
```

---

### 周5-6：ONNX模型转换

```python
import torch
import torch.onnx
import onnx
import onnxruntime as ort
import numpy as np

# ===== PyTorch → ONNX =====
class MyModel(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.conv = nn.Conv2d(3, 32, 3, padding=1)
        self.bn = nn.BatchNorm2d(32)
        self.relu = nn.ReLU()
        self.fc = nn.Linear(32 * 32 * 32, 10)
    
    def forward(self, x):
        x = self.conv(x)
        x = self.bn(x)
        x = self.relu(x)
        x = x.view(x.size(0), -1)
        x = self.fc(x)
        return x

model = MyModel()
model.load_state_dict(torch.load('cnn.pth'))
model.eval()

# 导出ONNX
dummy_input = torch.randn(1, 3, 32, 32)
torch.onnx.export(
    model,
    dummy_input,
    'model.onnx',
    export_params=True,
    opset_version=13,
    do_constant_folding=True,
    input_names=['input'],
    output_names=['output'],
    dynamic_axes={'input': {0: 'batch_size'}, 'output': {0: 'batch_size'}}
)

# ===== 验证ONNX =====
onnx_model = onnx.load('model.onnx')
onnx.checker.check_model(onnx_model)
print("ONNX模型验证通过！")

# ===== ONNX Runtime推理 =====
session = ort.InferenceSession(
    'model.onnx',
    providers=['CUDAExecutionProvider', 'CPUExecutionProvider']
)

input_name = session.get_inputs()[0].name
output_name = session.get_outputs()[0].name

input_data = np.random.randn(1, 3, 32, 32).astype(np.float32)
output = session.run([output_name], {input_name: input_data})[0]
print(f"ONNX输出形状: {output.shape}")

# ===== ONNX量化 =====
from onnxruntime.quantization import quantize_dynamic, QuantType

quantize_dynamic(
    'model.onnx',
    'model_quantized.onnx',
    weight_type=QuantType.QINT8
)
print("量化模型已保存: model_quantized.onnx")
```

---

### 周7-8：TensorRT部署

```python
# ===== TensorRT转换（需要torch2trt）=====
# pip install torch2trt

import torch
import torch2trt

class SimpleCNN(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(3, 32, 3, padding=1)
        self.conv2 = nn.Conv2d(32, 64, 3, padding=1)
        self.fc = nn.Linear(64 * 8 * 8, 10)
        self.pool = nn.MaxPool2d(2, 2)
        self.relu = nn.ReLU()
    
    def forward(self, x):
        x = self.relu(self.conv1(x))
        x = self.pool(x)
        x = self.relu(self.conv2(x))
        x = self.pool(x)
        x = x.view(x.size(0), -1)
        x = self.fc(x)
        return x

model = SimpleCNN().cuda().eval()
model.load_state_dict(torch.load('cnn.pth'))

# 创建输入张量
x = torch.randn(1, 3, 32, 32).cuda()

# 转换为TensorRT（FP16加速）
model_trt = torch2trt.torch2trt(
    model,
    [x],
    fp16_mode=True,
    max_workspace_size=1<<25,  # 256MB显存
    op_blocks=4
)

# 保存
torch.save(model_trt.state_dict(), 'model_trt.pth')

# ===== 性能对比 =====
import time

with torch.no_grad():
    # PyTorch推理
    for _ in range(100):
        y_torch = model(x)
    
    # TensorRT推理
    for _ in range(100):
        y_trt = model_trt(x)
    
    # 计时
    start = time.time()
    for _ in range(100):
        y_torch = model(x)
    torch_time = time.time() - start
    
    start = time.time()
    for _ in range(100):
        y_trt = model_trt(x)
    trt_time = time.time() - start
    
    print(f"PyTorch: {torch_time*10:.2f}ms/iter")
    print(f"TensorRT: {trt_time*10:.2f}ms/iter")
    print(f"加速比: {torch_time/trt_time:.2f}x")
```

---

## 阶段三：部署实战与项目（2-3个月）

### 目标
- 掌握完整的模型压缩部署流程
- 能够独立在Jetson/RK3588上部署模型
- 掌握llama.cpp边缘LLM部署
- 完成2-3个完整项目

---

### 周1-2：llama.cpp边缘LLM部署

```bash
# ===== 安装llama.cpp =====
git clone https://github.com/ggerganov/llama.cpp
cd llama.cpp
mkdir build && cd build
cmake .. && make -j$(nproc)

# ===== 下载模型 =====
# 从HuggingFace下载GGUF格式模型
huggingface-cli download TinyLlama/TinyLlama-1.1B-Chat-v1.0-GGUF \
    tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf --local-dir ./models

# ===== 命令行测试 =====
./build/bin/llama-cli \
    -m ./models/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf \
    -p "用一句话解释什么是嵌入式系统" \
    -n 128 \
    -t 4
```

```python
# ===== Python API使用llama.cpp =====
# pip install llama-cpp-python

from llama_cpp import Llama

class EdgeLLM:
    """边缘设备LLM推理类"""
    
    def __init__(self, model_path, n_ctx=2048, n_threads=4):
        self.model = Llama(
            model_path=model_path,
            n_ctx=n_ctx,
            n_threads=n_threads,
            n_gpu_layers=0,
            use_mmap=True,
            use_mlock=False,
            flash=False,
            verbose=False
        )
        
        # 预热
        self.model("热身", max_tokens=1)
        print("模型加载完成")
    
    def chat(self, messages, max_tokens=512, temperature=0.7):
        """对话生成"""
        response = self.model.create_chat_completion(
            messages=messages,
            max_tokens=max_tokens,
            temperature=temperature,
            stop=["</s>", "User:"]
        )
        return response['choices'][0]['message']['content']
    
    def stream_chat(self, messages, callback):
        """流式生成"""
        for token in self.model.create_chat_completion(
            messages=messages,
            max_tokens=512,
            stream=True
        ):
            content = token['choices'][0]['delta'].get('content', '')
            if content:
                callback(content)
    
    def benchmark(self, prompt="Hello, world!", iterations=50):
        """性能测试"""
        import time
        
        self.model(prompt, max_tokens=10)  # 预热
        
        start = time.time()
        for _ in range(iterations):
            self.model(prompt, max_tokens=32)
        elapsed = time.time() - start
        
        print(f"总耗时: {elapsed:.2f}s")
        print(f"平均延迟: {elapsed/iterations*1000:.0f}ms")
        print(f"生成速度: {iterations/elapsed:.1f} iter/s")

# ===== 使用示例 =====
def demo():
    llm = EdgeLLM(
        model_path="./models/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf",
        n_ctx=2048,
        n_threads=4
    )
    
    messages = [
        {"role": "system", "content": "你是一个智能助手。"},
        {"role": "user", "content": "用一句话解释什么是嵌入式AI。"}
    ]
    
    response = llm.chat(messages)
    print(f"回复: {response}")
    
    # 性能测试
    llm.benchmark()
```

---

### 周3-4：Jetson部署实战

```python
# ===== Jetson Nano/Orin部署检查清单 =====
"""
1. 安装JetPack（包含CUDA、TensorRT、cuDNN）
2. 安装PyTorch（aarch64版本）
3. 安装torch2trt
4. 模型转换与优化
5. 部署推理
"""

# ===== Jetson上的PyTorch安装 =====
"""
# 下载对应JetPack版本的PyTorch wheel
pip install torch torchvision --extra-index-url https://download.pytorch.org/whl/lr/
"""

# ===== Jetson推理类 =====
import cv2
import torch
import numpy as np
import time

class JetsonInference:
    """Jetson上的推理类"""
    
    def __init__(self, model_path, conf_thresh=0.5):
        self.conf_thresh = conf_thresh
        
        # 加载模型（假设已转换为TensorRT）
        self.model = self._load_model(model_path)
        self.model.eval()
        
        # 预热
        with torch.no_grad():
            for _ in range(10):
                _ = self.model(torch.randn(1, 3, 224, 224).cuda())
        
        print("模型加载完成，已预热")
    
    def _load_model(self, path):
        # 加载TensorRT模型
        import torch2trt
        model = self._create_model()
        model.load_state_dict(torch.load(path))
        model = model.cuda()
        
        # 转换为TensorRT
        x = torch.randn(1, 3, 224, 224).cuda()
        model_trt = torch2trt.torch2trt(
            model,
            [x],
            fp16_mode=True,
            max_workspace_size=1<<30
        )
        return model_trt
    
    def _create_model(self):
        # 创建模型结构
        import torch.nn as nn
        class ResNet(nn.Module):
            def __init__(self):
                super().__init__()
                self.features = nn.Sequential(
                    nn.Conv2d(3, 64, 7, 2, 3),
                    nn.BatchNorm2d(64),
                    nn.ReLU(inplace=True),
                    nn.MaxPool2d(3, 2, 1)
                )
                # ... 更多层
            def forward(self, x):
                return self.features(x)
        return ResNet()
    
    def infer(self, frame):
        """推理单帧
        
        Args:
            frame: numpy array, BGR格式
        
        Returns:
            results: 预测结果
        """
        # 预处理
        blob = cv2.dnn.blobFromImage(
            frame, 1/255.0, (224, 224),
            swapRB=True, crop=False
        )
        x = torch.from_numpy(blob).cuda()
        
        # 推理
        with torch.no_grad():
            output = self.model(x)
        
        return output.cpu().numpy()
    
    def benchmark(self, duration=10):
        """性能测试"""
        dummy_input = torch.randn(1, 3, 224, 224).cuda()
        
        with torch.no_grad():
            # 预热
            for _ in range(10):
                _ = self.model(dummy_input)
            
            # 计时
            start = time.time()
            count = 0
            while time.time() - start < duration:
                _ = self.model(dummy_input)
                count += 1
        
        fps = count / duration
        latency = 1000 / fps
        
        print(f"FPS: {fps:.2f}")
        print(f"延迟: {latency:.2f} ms")
        return fps, latency

# ===== 实时推理示例 =====
def realtime_inference():
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    
    infer = JetsonInference('model_trt.pth')
    
    while True:
        ret, frame = cap.read()
        if not ret:
            continue
        
        results = infer.infer(frame)
        
        # 绘制结果
        cv2.imshow('result', frame)
        if cv2.waitKey(1) == ord('q'):
            break
    
    cap.release()
    cv2.destroyAllWindows()
```

---

### 周5-8：综合项目实战

#### 项目1：端侧语音助手（推荐）

```
技术栈：ESP32/STM32 + 云端LLM + ASR/TTS
难度：★★★☆☆
时间：2周
功能：
  - 语音采集与唤醒词检测
  - 本地ASR预处理
  - 云端LLM对话
  - 本地TTS播放
学习收益：
  - 云边协同架构
  - 低功耗设计
  - 实时音频处理
```

#### 项目2：工业质检系统

```
技术栈：Jetson + TensorRT + YOLO/ResNet
难度：★★★☆☆
时间：2-3周
功能：
  - 摄像头实时采集
  - 缺陷检测与分类
  - 结果显示与报警
学习收益：
  - 工业部署实战
  - 实时推理优化
  - 系统集成能力
```

#### 项目3：本地LLM对话系统

```
技术栈：Jetson AGX Orin + llama.cpp + DeepSeek-R1
难度：★★★★☆
时间：3-4周
功能：
  - 本地大模型推理
  - 文档分析与问答
  - 知识库检索
学习收益：
  - LLM边缘部署
  - 模型量化优化
  - 生产级部署经验
```

---

## 阶段四：进阶方向（持续学习）

### 进阶方向选择

| 方向 | 所需技能 | 薪资水平 | 前景 |
|------|----------|----------|------|
| **NPU部署优化** | RKNN/SDK开发、算子优化 | 高 | 国产替代机会多 |
| **自动驾驶AI** | 感知融合、实时系统 | 很高 | 赛道火热 |
| **机器人AI** | 运动控制、规划算法 | 高 | 人形机器人风口的 |
| **AI芯片设计** | 体系结构、编译器 | 很高 | 卡脖子技术 |

### 持续学习资源

| 类型 | 资源 | 说明 |
|------|------|------|
| 课程 | 吴恩达《深度学习》 | 补基础 |
| 课程 | NVIDIA Deep Learning Institute | Jetson认证 |
| 书籍 | 《Embedded Machine Learning》 | 嵌入式AI专项 |
| 社区 | NVIDIA Jetson社区 | 最活跃 |
| 项目 | llama.cpp、TensorRTamples | 最佳实践 |
| 论文 | arxiv.org/abs/卷积网络相关 | 前沿研究 |

---

## 每日/每周学习计划

### 每日时间分配（建议）

| 时间段 | 内容 | 时长 |
|--------|------|------|
| 早上 | 理论学习（看视频/文档） | 1小时 |
| 下午 | 动手实践（敲代码） | 1.5小时 |
| 晚上 | 复盘总结（整理笔记） | 30分钟 |

### 周计划模板

```
周一~周二: 新知识学习（视频+文档）
周三~周四: 代码实践（跟着教程敲）
周五: 总结与整理（写笔记、项目）
周末: 自由探索（做小项目、刷GitHub）
```

### 学习检查清单

- [ ] PyTorch基础操作熟练
- [ ] 能独立训练MNIST分类模型
- [ ] 理解CNN、Transformer原理
- [ ] 掌握模型量化方法
- [ ] 能使用ONNX转换模型
- [ ] 能在Jetson上部署模型
- [ ] 完成1-2个完整项目

---

## 硬件准备清单

### 推荐硬件配置

| 类型 | 推荐型号 | 用途 | 价格区间 |
|------|----------|------|----------|
| **入门必买** | NVIDIA Jetson Nano（4GB） | 学习入门、跑小模型 | ¥800-1200 |
| **推荐配置** | NVIDIA Jetson Orin NX（16GB） | 生产级部署、学习进阶 | ¥2500-3500 |
| **高级设备** | NVIDIA Jetson AGX Orin（64GB） | 复杂模型、专业部署 | ¥6000-10000 |
| **低成本方案** | 树莓派5（8GB）+ USB加速棒 | 预算有限时的替代 | ¥600-1000 |
| **云端辅助** | AWS g4dn/xlarge 或 阿里云GPU | 训练、量化校准 | 按量付费 |

### 硬件选择建议

```
预算有限（<2000）: 树莓派5 + Coral USB
学习入门（2000-4000）: Jetson Nano 4GB
推荐配置（4000-6000）: Jetson Orin NX 16GB
专业部署（6000+）: Jetson AGX Orin 64GB
```

---

## 总结

### 核心观点

1. **嵌入式优势不要丢**：你的10年经验是稀缺资源
2. **聚焦部署而非研究**：成为"AI部署专家"而非"AI研究员"
3. **从小模型开始**：TinyLlama → 7B → 更大模型
4. **实战为王**：每个阶段都要有可运行的项目

### 关键技术里程碑

| 时间 | 里程碑 |
|------|--------|
| 1个月 | 能训练简单的MNIST分类器 |
| 2个月 | 掌握模型量化压缩 |
| 3个月 | 能在Jetson上部署优化模型 |
| 6个月 | 独立完成边缘LLM部署项目 |
| 12个月 | 成为嵌入式AI部署专家 |

### 行动建议

1. **今天就开始**：不要等"准备好了"再开始
2. **买一块开发板**： Jetson Nano或树莓派5
3. **跑通第一个例子**：从MNIST分类开始
4. **坚持6个月**：每天2-3小时

---

*祝学习顺利！有具体问题欢迎随时提问。*
