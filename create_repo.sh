#!/bin/bash
# Script to help create GitHub repository and invite collaborator
# Run this after creating the repository on GitHub

echo "=========================================="
echo "GitHub Repository Setup Helper"
echo "=========================================="
echo ""

# Check if GitHub CLI is installed
if command -v gh &> /dev/null; then
    echo "✓ GitHub CLI (gh) is installed"
    echo ""
    echo "Option 1: Create repository using GitHub CLI"
    echo "  gh repo create OS_Project_Phase_4 --private --source=. --remote=origin --push"
    echo ""
    echo "Option 2: Invite collaborator using GitHub CLI"
    echo "  gh api repos/\$(gh repo view --json owner,name -q '.owner.login + \"/\" + .name')/collaborators/Asgar-Fataymamode -X PUT -f permission=push"
    echo ""
else
    echo "GitHub CLI not installed. Using manual method."
    echo ""
fi

echo "=========================================="
echo "Manual Setup Steps:"
echo "=========================================="
echo ""
echo "1. Create repository on GitHub:"
echo "   https://github.com/new"
echo "   - Name: OS_Project_Phase_4"
echo "   - Private: Yes"
echo "   - Don't initialize with README"
echo ""
echo "2. After creating, run these commands:"
echo ""
read -p "Enter your GitHub username: " GITHUB_USER
echo ""
echo "git remote add origin https://github.com/${GITHUB_USER}/OS_Project_Phase_4.git"
echo "git branch -M main"
echo "git push -u origin main"
echo ""
echo "3. Invite collaborator:"
echo "   - Go to: https://github.com/${GITHUB_USER}/OS_Project_Phase_4/settings/access"
echo "   - Click 'Add people'"
echo "   - Enter: Asgar-Fataymamode"
echo "   - Permission: Write"
echo ""
echo "=========================================="

