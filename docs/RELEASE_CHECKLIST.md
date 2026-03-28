# 发布检查清单 (Release Checklist)

## 发布前准备

### 代码质量
- [ ] 运行所有测试并通过
- [ ] 运行 clang-format 格式化代码
- [ ] 运行 clang-tidy 静态分析
- [ ] 确认无编译警告
- [ ] 更新 CHANGELOG.md

### 文档
- [ ] 更新 README.md（如有必要）
- [ ] 生成 API 文档
- [ ] 更新 SETUP_GUIDE.md（如有必要）
- [ ] 确认文档与代码一致

### 版本管理
- [ ] 更新版本号（CMakeLists.txt, README.md, Doxyfile）
- [ ] 创建 Git 标签：`git tag -a v1.0.0 -m "Release v1.0.0"`
- [ ] 推送标签：`git push origin v1.0.0`

### CI/CD
- [ ] 确认 GitHub Actions 构建成功
- [ ] 确认所有平台构建通过
- [ ] 确认文档生成成功

## 发布步骤

### 1. 创建 Release
```bash
# 在 GitHub 上创建 Release
# 或使用命令行
gh release create v1.0.0 --generate-notes
```

### 2. 上传构建产物
- [ ] 上传 ELF 文件
- [ ] 上传 BIN 文件
- [ ] 上传 API 文档

### 3. 通知
- [ ] 发布发布公告
- [ ] 通知团队成员
- [ ] 更新项目主页

## 发布后

### 验证
- [ ] 下载 Release 并测试
- [ ] 验证文档可访问
- [ ] 验证示例代码可运行

### 后续工作
- [ ] 创建新的开发分支
- [ ] 更新开发计划
- [ ] 收集用户反馈

---

## 快速发布命令

```bash
# 1. 更新版本号
# 编辑 CMakeLists.txt, README.md, Doxyfile

# 2. 提交更改
git add .
git commit -m "chore: 准备发布 v1.0.0"

# 3. 创建标签
git tag -a v1.0.0 -m "Release v1.0.0"

# 4. 推送
git push origin main
git push origin v1.0.0

# 5. 创建 Release（需要 GitHub CLI）
gh release create v1.0.0 --generate-notes
```

---

## 版本号更新位置

1. `CMakeLists.txt`: `PROJECT_VERSION_MAJOR/MINOR/PATCH`
2. `README.md`: `**版本**：1.0.0`
3. `Doxyfile`: `PROJECT_NUMBER = "1.0.0"`
4. `src/app/app_config.h`: `APP_VERSION_*`
5. `CHANGELOG.md`: 添加新版本条目
