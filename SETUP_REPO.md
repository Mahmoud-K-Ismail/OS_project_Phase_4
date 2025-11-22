# Repository Setup Instructions

## Step 1: Create GitHub Repository

1. Go to https://github.com/new
2. Repository name: `OS_Project_Phase_4` (or your preferred name)
3. Description: "Operating Systems Phase 4 - Scheduler-Driven Remote Shell"
4. Choose **Private** (recommended for academic projects)
5. **DO NOT** initialize with README, .gitignore, or license (we already have these)
6. Click "Create repository"

## Step 2: Add Remote and Push

After creating the repository, GitHub will show you commands. Run these in your project directory:

```bash
# Add remote (replace YOUR_USERNAME with your GitHub username)
git remote add origin https://github.com/YOUR_USERNAME/OS_Project_Phase_4.git

# Or if using SSH:
git remote add origin git@github.com:YOUR_USERNAME/OS_Project_Phase_4.git

# Stage all files
git add .

# Create initial commit
git commit -m "Initial commit: Phase 4 Scheduler-Driven Remote Shell

- Implemented combined RR + SJRF scheduling algorithm
- Added scheduler thread with quantum-based execution
- Created demo program for workload simulation
- Comprehensive testing with 3 required scenarios
- Enhanced code comments and documentation
- Complete Phase 4 report"

# Push to GitHub
git branch -M main
git push -u origin main
```

## Step 3: Invite Collaborator

1. Go to your repository on GitHub: `https://github.com/YOUR_USERNAME/OS_Project_Phase_4`
2. Click on **Settings** (top menu)
3. Click on **Collaborators** (left sidebar)
4. Click **Add people**
5. Enter: `Asgar-Fataymamode` or their GitHub username/email
6. Select permission level: **Write** (full access)
7. Click **Add [username] to this repository**
8. They will receive an email invitation to accept

## Alternative: Using GitHub CLI

If you have GitHub CLI installed:

```bash
# Create repository
gh repo create OS_Project_Phase_4 --private --source=. --remote=origin --push

# Invite collaborator
gh api repos/:owner/:repo/collaborators/Asgar-Fataymamode -X PUT -f permission=push
```

## Step 4: Verify Setup

```bash
# Check remote is set
git remote -v

# Check status
git status

# View commit history
git log --oneline
```

## Repository Structure

The repository includes:
- Source files: `*.c`, `*.h`
- Makefile
- Documentation: `README.md`, `docs/Phase4_Report.md`
- Test scripts: `test_scenario*.sh`
- Test data: `tests/`

Excluded (via .gitignore):
- Compiled binaries
- Object files
- Test logs
- Temporary files

