// Global state
let currentBrowserPath = '/', selectedFile = null, currentStats = null;
let selectedDesign = null, progressInterval;
let bootScriptsSelected = [];
let scriptChanged = false;
let lastErrorState = ""; // Track state to avoid redundant Issue tab re-renders

// Trigger browser warning for unsaved changes
window.addEventListener('beforeunload', (e) => {
    if (scriptChanged) {
        e.preventDefault();
        e.returnValue = ''; // Required for Chrome/Firefox
        return "Unsaved changes will be lost!";
    }
});

// UI Initialization
document.addEventListener('DOMContentLoaded', () => {
    const scriptArea = document.getElementById('scriptArea');
    const highlights = document.getElementById('editorHighlights');
    const gutter = document.getElementById('editorGutter');

    if (scriptArea) {
        const syncScroll = () => {
            highlights.scrollTop = scriptArea.scrollTop;
            highlights.scrollLeft = scriptArea.scrollLeft;
            gutter.scrollTop = scriptArea.scrollTop;
        };

        let debounceTimer;
        scriptArea.addEventListener('input', () => {
            updateGutter();
            scriptChanged = true;
            syncScroll();
            
            fastUpdateHighlights();
            
            clearTimeout(debounceTimer);
            debounceTimer = setTimeout(() => {
                updateErrorLens();
            }, 500); 
        });
        
        scriptArea.addEventListener('scroll', syncScroll);
        
        scriptArea.addEventListener('paste', () => {
            setTimeout(() => {
                updateGutter();
                updateErrorLens();
                syncScroll();
            }, 0);
        });

        scriptArea.addEventListener('keydown', (e) => {
            if (e.key === 'Backspace') {
                const start = scriptArea.selectionStart;
                const end = scriptArea.selectionEnd;
                if (start === end && start > 0) {
                    const text = scriptArea.value;
                    const before = text.substring(0, start);
                    const lineStart = before.lastIndexOf('\n') + 1;
                    const lineText = before.substring(lineStart);
                    if (lineText.length > 0 && !/[^\s\t]/.test(lineText)) {
                        e.preventDefault();
                        scriptArea.value = text.substring(0, lineStart) + text.substring(end);
                        scriptArea.selectionStart = scriptArea.selectionEnd = lineStart;
                        updateGutter();
                        updateErrorLens();
                        return;
                    }
                }
            }
            if (e.key === 'Tab') {
                if (typeof autocompletePopup !== 'undefined' && autocompletePopup && autocompletePopup.style.display === 'block') return;
                e.preventDefault();
                const start = scriptArea.selectionStart;
                const end = scriptArea.selectionEnd;
                scriptArea.value = scriptArea.value.substring(0, start) + "\t" + scriptArea.value.substring(end);
                scriptArea.selectionStart = scriptArea.selectionEnd = start + 1;
                updateErrorLens();
                syncScroll();
            } else if (e.key === 'Enter') {
                if (typeof autocompletePopup !== 'undefined' && autocompletePopup && autocompletePopup.style.display === 'block') return;
                e.preventDefault();
                const start = scriptArea.selectionStart;
                const value = scriptArea.value;
                const beforeCursor = value.substring(0, start);
                const lineStart = beforeCursor.lastIndexOf('\n') + 1;
                const currentLine = beforeCursor.substring(lineStart);
                const indentMatch = currentLine.match(/^(\s*)/);
                let indent = indentMatch ? indentMatch[1] : '';
                const trimmedLine = currentLine.trim().toUpperCase();

                // Auto-indent next line if current line starts a block
                const isFunctionBlockStart = trimmedLine.startsWith('FUNCTION') || trimmedLine.startsWith('DEF_');
                const isControlBlockStart = trimmedLine.startsWith('IF ') || trimmedLine.startsWith('IF:') || 
                                           trimmedLine.startsWith('FOR ') || trimmedLine.startsWith('FOR:') || 
                                           trimmedLine.startsWith('WHILE ') || trimmedLine.startsWith('WHILE:') || 
                                           trimmedLine.startsWith('REPEAT ') || trimmedLine.startsWith('REPEAT:') ||
                                           trimmedLine === 'ELSE' || trimmedLine === 'ELSE:' ||
                                           trimmedLine.startsWith('ELIF ') || trimmedLine.startsWith('ELIF:');
                const isFunctionLabelStart = trimmedLine.includes('FUNCTION_') && trimmedLine.endsWith('():');
                const isColonFunctionStart = trimmedLine.endsWith(':') && (trimmedLine.startsWith('FUNCTION') || trimmedLine.includes('():'));

                if (isFunctionBlockStart || isControlBlockStart || isFunctionLabelStart || isColonFunctionStart) {
                    indent += '\t';
                }

                // Automatic de-indent for closing blocks ON ENTER
                if (trimmedLine.startsWith('ENDIF') || trimmedLine.startsWith('END_IF') || trimmedLine.startsWith('ENDFOR') || trimmedLine.startsWith('END_FOR') || 
                    trimmedLine.startsWith('END_FUNCTION') || trimmedLine.startsWith('END_DEF') || 
                    trimmedLine.startsWith('END_WHILE') || trimmedLine.startsWith('END_REPEAT') ||
                    trimmedLine.startsWith('END_RUN_ON_REBOOT')) {
                    const currentIndent = (currentLine.match(/^\s*/) || [""])[0];
                    if (currentIndent.length > 0) {
                        const newIndent = currentIndent.substring(0, currentIndent.length - 1);
                        const newLineText = newIndent + currentLine.trim();
                        scriptArea.value = value.substring(0, lineStart) + newLineText + value.substring(start);
                        const newPos = lineStart + newLineText.length;
                        scriptArea.setSelectionRange(newPos, newPos);
                        indent = newIndent; // New line will follow this de-indent
                    }
                }

                const insertion = "\n" + indent;
                const newStart = scriptArea.selectionStart;
                scriptArea.value = scriptArea.value.substring(0, newStart) + insertion + scriptArea.value.substring(scriptArea.selectionEnd);
                scriptArea.selectionStart = scriptArea.selectionEnd = newStart + insertion.length;
                updateGutter();
                updateErrorLens();
                syncScroll();
            }
        });

        const editorMain = document.querySelector('.editor-main');
        const syncHover = (e) => {
            const rect = scriptArea.getBoundingClientRect();
            const y = e.clientY - rect.top + scriptArea.scrollTop;
            const lineIdx = Math.floor((y - 15) / 22);
            
            const highlightLines = highlights.children;
            for (let i = 0; i < highlightLines.length; i++) {
                if (i === lineIdx) {
                    highlightLines[i].classList.add('is-hovered');
                } else {
                    highlightLines[i].classList.remove('is-hovered');
                }
            }
        };

        if (editorMain) {
            editorMain.addEventListener('mousemove', syncHover);
            editorMain.addEventListener('mouseleave', () => {
                Array.from(highlights.children).forEach(el => el.classList.remove('is-hovered'));
            });
        }

        updateGutter();
        updateErrorLens();
        syncScroll();
    }
    
    updateStats();
    initFileManager();
    initMacosDock(document.querySelector('.tab'));
    
    // Pre-populate state
    updateStats();
    initMacosDock(document.querySelector('.control-panel'));
    
    document.getElementById('autoRetryToggle').checked = localStorage.getItem('autoRetryConn') !== 'false';
    
    setInterval(updateStats, 5000);
    setInterval(pollSystemStatus, 3000);
    setInterval(refreshTasks, 10000);

    // Custom Scrollbar Init
    setTimeout(initAllCustomScrollbars, 600);
    setTimeout(initAutocomplete, 500);
    
    // Initialize Resizable Editor
    initResizableEditor();
});

function initResizableEditor() {
    const editor = document.querySelector('.editor-wrapper');
    if (!editor) return;

    const handle = document.createElement('div');
    handle.className = 'editor-resizer';
    editor.appendChild(handle);

    let isResizing = false;
    let startX, startY, startWidth, startHeight;

    handle.addEventListener('mousedown', (e) => {
        isResizing = true;
        startX = e.clientX;
        startY = e.clientY;
        startWidth = editor.offsetWidth;
        startHeight = editor.offsetHeight;
        document.body.style.userSelect = 'none';
        document.body.style.cursor = 'all-scroll';
        handle.classList.add('active');
    });

    document.addEventListener('mousemove', (e) => {
        if (!isResizing) return;
        const deltaX = e.clientX - startX;
        const deltaY = e.clientY - startY;
        
        const newWidth = startWidth + deltaX;
        const newHeight = startHeight + deltaY;
        
        if (newHeight >= 150 && newHeight <= 1200) {
            editor.style.height = `${newHeight}px`;
        }
        if (newWidth >= 400 && newWidth <= 1600) {
            editor.style.width = `${newWidth}px`;
        }
        
        // Trigger scroll sync and updates
        if (typeof updateGutter === 'function') updateGutter();
        if (typeof syncScroll === 'function') {
            const scriptArea = document.getElementById('scriptArea');
            if (scriptArea) syncScroll.call(scriptArea);
        }
    });

    document.addEventListener('mouseup', () => {
        if (isResizing) {
            isResizing = false;
            document.body.style.userSelect = '';
            document.body.style.cursor = '';
            handle.classList.remove('active');
        }
    });
}

// =============================================
// Tab Navigation (Fixes mobile switching bug)
// =============================================
function openTab(evt, tabName) {
    const tabs = document.querySelectorAll('.tabcontent');
    tabs.forEach(t => t.style.display = 'none');
    const links = document.querySelectorAll('.tablinks');
    links.forEach(l => l.classList.remove('active'));
    const target = document.getElementById('tab-' + tabName);
    if (target) target.style.display = 'block';
    if (evt && evt.currentTarget) evt.currentTarget.classList.add('active');
    // Lazy-load tab data on switch
    if (tabName === 'Scripts') setTimeout(refreshFiles, 50);
    if (tabName === 'File_Manager') setTimeout(refreshFileBrowser, 50);
    if (tabName === 'Boot') setTimeout(refreshBootScripts, 50);
    if (tabName === 'Statistics') setTimeout(updateStats, 50);
    if (tabName === 'Settings') setTimeout(initSettingsTab, 50);
}

// =============================================
// System Status Polling
// =============================================
function pollSystemStatus() {
    fetch('/api/stats')
        .then(r => r.json())
        .then(data => {
            const statusEl = document.getElementById('scriptStatus');
            if (statusEl) statusEl.textContent = data.scriptRunning ? 'Running...' : 'Idle';
            const progBar = document.getElementById('progressBar');
            const progFill = document.getElementById('progressFill');
            if (progBar && progFill) {
                if (data.delayProgress > 0) {
                    progBar.style.display = 'block';
                    const secs = (data.delayTotal / 1000).toFixed(1);
                    progFill.style.width = data.delayProgress + '%';
                    progFill.title = `Delay: ${secs}s`;
                } else {
                    progBar.style.display = 'none';
                    progFill.style.width = '0%';
                }
            }
        }).catch(() => {});
}

function toggleIssuesList() {
    const panel = document.getElementById('issuesPanel');
    if (panel) {
        panel.classList.toggle('expanded');
    }
}

function toggleAdvancedErrorOptions() {
    const el = document.getElementById('advancedErrorOptions');
    const arrow = document.getElementById('advErrorArrow');
    if (!el) return;
    const isVisible = el.style.display === 'block';
    el.style.display = isVisible ? 'none' : 'block';
    if (arrow) arrow.style.transform = isVisible ? 'rotate(0deg)' : 'rotate(180deg)';
}

function toggleAutoRetry() {
    const toggle = document.getElementById('autoRetryToggle');
    localStorage.setItem('autoRetryConn', toggle.checked);
}

let statsController = null;
let statusController = null;
let cachedLanguageList = [];
let languagesDiscovered = false;
let globalDeclaredVars = new Set();
let globalDeclaredFunctions = new Set();
let ignoredWarnings = new Set();

function toggleExample(el) {
    el.classList.toggle('active');
}

function ignoreWarning(line, varName, btn) {
    const lineEl = btn ? btn.closest('.warning-line') : null;
    if (lineEl) {
        // Only fade warning elements, NOT the code
        lineEl.style.transition = 'background 0.6s cubic-bezier(0.4, 0, 0.2, 1)';
        lineEl.style.background = 'transparent';
        
        const message = lineEl.querySelector('.inline-warning');
        const button = lineEl.querySelector('.ignore-btn-inline');
        const code = lineEl.querySelector('.warning-text') || lineEl.querySelector('.line-code');
        
        if (message) {
            message.style.transition = 'all 0.6s ease';
            message.style.opacity = '0';
            message.style.filter = 'blur(10px)';
        }
        if (button) {
            button.style.transition = 'all 0.6s ease';
            button.style.opacity = '0';
            button.style.transform = 'translateY(-50%) scale(0.8)';
        }
        if (code) {
            code.style.transition = 'all 0.6s ease';
            code.style.filter = 'none';
            code.style.opacity = '1';
        }
        
        setTimeout(() => {
            ignoredWarnings.add(`${line}-${varName}`);
            updateErrorLens();
        }, 600);
    } else {
        ignoredWarnings.add(`${line}-${varName}`);
        updateErrorLens();
    }
}

function initMacosDock(container) {
    if (!container) return;
    // Disable JS-based animation on mobile/touch to save resources and avoid "apple bar effect"
    if (window.matchMedia("(max-width: 767px)").matches || ('ontouchstart' in window)) return;
    
    const items = Array.from(container.querySelectorAll('.tablinks'));
    const state = items.map(item => ({ el: item, currentScale: 1, targetScale: 1 }));
    
    let lastMouseX = 0, lastMouseY = 0, lastTime = 0;
    let velocity = 0, isMouseIn = false, mouseX = 0;

    container.addEventListener('mousemove', (e) => {
        const now = performance.now();
        const dt = now - lastTime;
        if (dt > 0) {
            const dx = e.clientX - lastMouseX;
            const dy = e.clientY - lastMouseY;
            velocity = velocity * 0.8 + (Math.sqrt(dx*dx + dy*dy) / dt) * 0.2;
        }
        lastMouseX = e.clientX; lastMouseY = e.clientY; lastTime = now;
        mouseX = e.clientX;
        isMouseIn = true;
    });

    container.addEventListener('mouseleave', () => {
        isMouseIn = false;
        velocity = 0;
    });

    function animate() {
        const vFactor = Math.max(0, Math.min(1, 1 - velocity / 3.0));
        
        state.forEach(s => {
            if (isMouseIn) {
                const rect = s.el.getBoundingClientRect();
                const centerX = rect.left + rect.width / 2;
                const dist = Math.abs(mouseX - centerX);
                const maxDist = 130;
                
                if (dist < maxDist) {
                    const intensity = (1 - dist / maxDist) * vFactor;
                    s.targetScale = 1 + (0.3 * intensity);
                } else {
                    s.targetScale = 1;
                }
            } else {
                s.targetScale = 1;
            }

            // Lerp for ultra-smoothness
            s.currentScale += (s.targetScale - s.currentScale) * 0.15;
            s.el.style.transform = `scale(${s.currentScale})`;
            s.el.style.zIndex = s.currentScale > 1.05 ? "10" : "1";
        });
        
        requestAnimationFrame(animate);
    }
    animate();
}

function updateGutter() {
    const scriptArea = document.getElementById('scriptArea');
    const gutter = document.getElementById('editorGutter');
    if (!scriptArea || !gutter) return;
    const lines = scriptArea.value.split('\n').length;
    let html = '';
    for (let i = 1; i <= lines; i++) html += `<div>${i}</div>`;
    gutter.innerHTML = html;
}

function pollSystemStatus() {
    if (statusController) statusController.abort();
    statusController = new AbortController();

    fetch('/status', { signal: statusController.signal }).then(r => r.text()).then(data => {
        const statusEl = document.getElementById('scriptStatus');
        if (statusEl && !statusEl.classList.contains('status-error')) {
            statusEl.textContent = data.replace(/[\uD800-\uDBFF][\uDC00-\uDFFF]|\u200D|\uFE0F/g, '');
        }
    }).catch(err => {
        if (err.name !== 'AbortError') console.error("Status Poll Error:", err);
    });
}

function openTab(evt, tabName) {
    const tablinks = document.getElementsByClassName('tablinks');
    for (let i = 0; i < tablinks.length; i++) tablinks[i].classList.remove('active');
    
    if (evt) evt.currentTarget.classList.add('active');
    else Array.from(tablinks).forEach(btn => { if (btn.textContent.includes(tabName)) btn.classList.add('active'); });

    const tabcontents = document.getElementsByClassName('tabcontent');
    for (let i = 0; i < tabcontents.length; i++) tabcontents[i].style.display = 'none';
    
    const target = document.getElementById('tab-' + tabName);
    if (target) target.style.display = 'block';

    if (tabName === 'Scripts') refreshFiles();
    if (tabName === 'Boot') refreshBootScripts();
    if (tabName === 'Statistics') updateStats();
    if (tabName === 'File_Manager') refreshFileBrowser();
    if (tabName === 'Design') refreshDesigns();
    if (tabName === 'Settings') initSettingsTab();
}

function applyHighlighting(line) {
    if (!line.trim()) return '&nbsp;';
    
    // 1. Handle comments immediately
    if (line.trim().startsWith('REM') || line.trim().startsWith('//')) {
        const escapedComment = line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        return `<span class="syntax-comment">${escapedComment}</span>`;
    }

    const cat = {
        text: ['STRING', 'STRINGLN'],
        logic: ['IF', 'ELSE', 'ELIF', 'ENDIF', 'END_IF', 'REPEAT', 'END_REPEAT', 'WHILE', 'END_WHILE', 'FOR', 'FOR:', 'ENDFOR', 'END_FOR', 'WAIT_FOR_EVENT', 'RUN_AT_TIME', 'RUN_AT_DAY', 'RUN_WHEN_WIFI', 'IS_ONLINE', 'IS_OFFLINE', 'WIFI_OFF_WHEN_WIFI', 'WIFI_ON_WHEN_WIFI', 'BLUETOOTH_OFF_WHEN_WIFI', 'BLUETOOTH_ON_WHEN_WIFI', 'IF_CLIENT_CONNECTED_BLUETOOTH', 'IF_CLIENT_CONNECTED_WIFI', 'IF_CLIENT_DISCONNECTED_WIFI', 'IF_CLIENT_DISCONNECTED_BLUETOOTH', 'IF_CLIENT_CONNECTED', 'IF_CLIENT_DISCONNECTED', 'IF_CLIENT_CONNECTED_DISCONNECTED', 'IF_CLIENT_CONNECTED_DISCONNECTED_BLUETOOTH', 'IF_CLIENT_CONNECTED_DISCONNECTED_WIFI', 'RUN_ON_REBOOT', 'END_RUN_ON_REBOOT', 'BLUETOOTH_DISCOVERY', 'RUN_WHEN_BLUETOOTH_FOUND', 'RUN_WHEN_BT_FOUND', 'BT_FOUND', 'FROM', 'TO', 'STEP', 'FUNCTION', 'DEF_', 'END_FUNCTION', 'END_DEF', 'BEGIN_ROWER', 'END_ROWER', 'IF_NOT_PRESENT', 'LOCALE', 'LOCALE_DE', 'LOCALE_EN', 'LOCALE_FR', 'LOCALE_ES', 'LOCALE_IT', 'LOCALE_UK'],
        delay: ['DELAY', 'DEFAULTDELAY', 'DEFAULT_DELAY'],
        mod: ['GUI', 'CTRL', 'ALT', 'SHIFT', 'CAPSLOCK', 'WINDOWS', 'CONTROL'],
        special: ['ENTER', 'TAB', 'ESC', 'ESCAPE', 'BACKSPACE', 'DELETE', 'DEL', 'HOME', 'END', 'PAGEUP', 'PAGEDOWN', 'F1','F2','F3','F4','F5','F6','F7','F8','F9','F10','F11','F12', 'SPACE', 'PAUSE', 'BREAK', 'INSERT', 'PRINTSCREEN', 'SCROLLLOCK', 'MENU', 'APP', 'UP', 'UPARROW', 'DOWN', 'DOWNARROW', 'LEFT', 'LEFTARROW', 'RIGHT', 'RIGHTARROW', 'NUMLOCK'],
        sys: ['REBOOT', 'SHUTDOWN', 'PING', 'HTTP_REQUEST', 'GET_TIME', 'GET_DAY', 'DOWNLOAD_FILE', 'UPLOAD_FILE', 'LED_R', 'LED_G', 'LED_B', 'LED_Y', 'LED_W', 'LED_O', 'LED_P', 'LED_C', 'LED_M', 'LED_A', 'LED_OFF', 'LED_STOP', 'SELFDESTRUCT', 'WIFI_ON', 'WIFI_OFF', 'BLUETOOTH_ON', 'BLUETOOTH_OFF', 'JOIN_INTERNET', 'LEAVE_INTERNET', 'RANDOM_CHAR', 'RANDOM_NUMBER', 'RANDOM_SPECIAL', 'DETECT_OS', 'HOLD_TILL_STRING', 'LED_BLINK', 'BLINK_STOP', 'BLINK_LED_R', 'BLINK_LED_G', 'BLINK_LED_B', 'BLINK_LED_V', 'BLINK_LED_A', 'LED', 'VID_', 'PID_', 'MAN_', 'PRODUCT_']
    };

    let html = line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    let tokens = [];
    const addToken = (cls, content) => {
        const id = `@@TOKEN${tokens.length}@@`;
        tokens.push(`<span class="syntax-${cls}">${content}</span>`);
        return id;
    };

    // 1. Interpolations (Shielded early)
    html = html.replace(/\${([^}]*)}/g, (m, p1) => {
        return addToken('cmd-logic', '${') + p1 + addToken('cmd-logic', '}');
    });

    // 2. STRING / STRINGLN (Everything after is yellow, except shielded interpolations)
    const stringMatch = html.match(/^(\s*)(STRING|STRINGLN)(\s|$)(.*)/i);
    if (stringMatch) {
        const space = stringMatch[1];
        const cmd = stringMatch[2].toUpperCase();
        const sep = stringMatch[3] || "";
        let content = stringMatch[4] || "";
        if (content) {
            content = content.replace(/(\b(VAR_[a-zA-Z0-9_]*|VARIABLE_[a-zA-Z0-9_]*)\b|\$[a-zA-Z0-9_]+)/gi, (m) => {
                let v = m.toUpperCase();
                if (v.startsWith('$')) v = v.substring(1);
                
                if (globalDeclaredVars.has(v)) {
                    return addToken('var-in-string', m);
                }
                return m;
            });
        }
        html = space + addToken('cmd-text', cmd) + sep + (content ? addToken('string', content) : "");
    } else {
        // 3. Normal Strings in quotes
        html = html.replace(/"([^"]*)"/g, (m) => addToken('string', m));

        // 4. Keyword Prefixes (Integrated names like VAR_target_os)
        html = html.replace(/\b(VAR_|VARIABLE_|LOCALE_|VID_|PID_|MAN_|PRODUCT_)([a-zA-Z0-9_]*)/gi, (m, p1, p2) => {
            let catName = 'cmd-logic';
            const upperP1 = p1.toUpperCase();
            if (['VID_', 'PID_', 'MAN_', 'PRODUCT_'].includes(upperP1)) catName = 'cmd-sys';
            // Only highlight the prefix as green, keep the name (p2) white
            if (['VAR_', 'VARIABLE_'].includes(upperP1)) return addToken('cmd-logic', p1) + p2;
            return addToken(catName, p1) + p2;
        });

        // 5. Category Keywords
        Object.keys(cat).forEach(category => {
            cat[category].forEach(word => {
                let regex;
                if (word.endsWith('_')) {
                    // Prefix commands like VID_ or LOCALE_ don't need a trailing word boundary
                    regex = new RegExp(`\\b(${word})`, 'gi');
                } else if (word.endsWith(':')) {
                    // Match commands with a colon like FOR:
                    regex = new RegExp(`\\b(${word})`, 'gi');
                } else {
                    regex = new RegExp(`\\b(${word})\\b`, 'gi');
                }
                html = html.replace(regex, (m) => addToken(`cmd-${category}`, m));
            });
        });

        // 6. Assignments (Preserve exact spacing to prevent cursor drift)
        html = html.replace(/^(\s*)([a-zA-Z_][a-zA-Z0-9_]*)(\s*)=/g, (m, p1, p2, p3) => {
            const upper = p2.toUpperCase();
            if (Object.values(cat).flat().includes(upper)) return m;
            return p1 + p2 + p3 + '=';
        });

        // 7. Functions & Labels
        html = html.replace(/\b([a-zA-Z_][a-zA-Z0-9_]*)(\(\):?|\s*\()/g, (m, p1, p2) => {
            return addToken('cmd-sys', p1) + p2;
        });

        // 8. Numbers
        html = html.replace(/\b(\d+)\b/g, (m) => addToken('num', m));

        // 9. Operators
        html = html.replace(/([+\-*/%^])/g, (m) => addToken('op', m));

        // 10. Variables ($)
        html = html.replace(/(\$[a-zA-Z0-9_]+)/g, '$1');
    }

    // Final: Global Token Replacement (Regex to prevent partial matches like TOKEN1 vs TOKEN11)
    for (let i = tokens.length - 1; i >= 0; i--) {
        const tokenID = `@@TOKEN${i}@@`;
        html = html.split(tokenID).join(tokens[i]);
    }

    return html;
}


const STRUCTURAL_KEYWORDS = new Set([
    'IF', 'ELSE', 'ELIF', 'ENDIF', 'END_IF', 'FOR', 'ENDFOR', 'END_FOR', 
    'WHILE', 'END_WHILE', 'REPEAT', 'END_REPEAT', 'FUNCTION', 'DEF_', 
    'END_FUNCTION', 'END_DEF', 'RUN_ON_REBOOT', 'END_RUN_ON_REBOOT'
]);

const VALID_KEYWORDS = new Set([
    'STRING', 'STRINGLN', 'DELAY', 'GUI', 'CTRL', 'ALT', 'SHIFT', 'ENTER', 'TAB', 'ESC', 'VAR', 'VARIABLE', 'IF', 'ELSE', 'ELIF', 'ENDIF', 'END_IF', 'REPEAT', 
    'HTTP_REQUEST', 'HTTPS_REQUEST', 'GET_TIME', 'GET_DAY', 'RUN_AT_TIME', 'RUN_AT_DAY', 'RUN_WHEN_WIFI', 'WAIT_FOR_EVENT', 'REM', 'DEFAULTDELAY', 'DEFAULT_DELAY',
    'FOR', 'ENDFOR', 'END_FOR', 'MENU', 'APP', 'CAPSLOCK', 'DELETE', 'BACKSPACE', 'HOME', 'END', 'PAGEUP', 'PAGEDOWN', 'PRINTSCREEN', 'SCROLLLOCK', 
    'PAUSE', 'BREAK', 'INSERT', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12', 'SPACE', 'REBOOT', 'SHUTDOWN', 'PING', 
    'DOWNLOAD_FILE', 'UPLOAD_FILE', 'LED_R', 'LED_G', 'LED_B', 'LED_Y', 'LED_W', 'LED_O', 'LED_P', 'LED_C', 'LED_M', 'LED_A', 'LED_V', 'LED_OFF',
    'BLINK_LED_R', 'BLINK_LED_G', 'BLINK_LED_B', 'BLINK_LED_V', 'LED_STOP', 'SELFDESTRUCT', 'WIFI_ON', 'WIFI_OFF', 'BLUETOOTH_ON', 'BLUETOOTH_OFF', 
    'JOIN_INTERNET', 'LEAVE_INTERNET', 'WIFI_OFF_WHEN_WIFI', 'WIFI_ON_WHEN_WIFI', 'BLUETOOTH_OFF_WHEN_WIFI', 'BLUETOOTH_ON_WHEN_WIFI',
    'IF_PRESENT', 'IF_NOTPRESENT', 'IF_BT_PRESENT', 'IF_ONLINE', 'IF_OFFLINE', 'IF_OS', 'IF_DETECT_OS_INCLUDES', 'IF_NOT_PRESENT',
    'IF_CLIENT_CONNECTED_BLUETOOTH', 'IF_CLIENT_CONNECTED_WIFI', 'IF_CLIENT_DISCONNECTED_WIFI', 'IF_CLIENT_DISCONNECTED_BLUETOOTH', 
    'IF_CLIENT_CONNECTED', 'IF_CLIENT_DISCONNECTED', 'IF_CLIENT_CONNECTED_DISCONNECTED', 'IF_CLIENT_CONNECTED_DISCONNECTED_BLUETOOTH', 
    'IF_CLIENT_CONNECTED_DISCONNECTED_WIFI', 'DETECT_OS', 'WAIT_FOR_SD', 'HOLD', 'KEYCODE', 'HOLD_TILL_STRING', 'CD', 'SET_BUTTON_PIN', 'RUN_PAYLOAD',
    'FUNCTION', 'DEF_', 'RANDOM_CHAR', 'RANDOM_NUMBER', 'RANDOM_SPECIAL', 'END_FUNCTION', 'END_DEF', 'BEGIN_ROWER', 'END_ROWER',
    'UPARROW', 'DOWNARROW', 'LEFTARROW', 'RIGHTARROW', 'UP', 'DOWN', 'LEFT', 'RIGHT', 'ESCAPE', 'DEL', 'WINDOWS', 'CONTROL', 'NUMLOCK', 'FROM', 'TO', 'STEP', 'LOCALE',
    'VID_', 'PID_', 'MAN_', 'PRODUCT_', 'LED_BLINK', 'BLINK_STOP', 'RUN_ON_REBOOT', 'END_RUN_ON_REBOOT', 'BLUETOOTH_DISCOVERY', 
    'RUN_WHEN_BLUETOOTH_FOUND', 'RUN_WHEN_BT_FOUND', 'BT_FOUND',
    'RANDOM_VID', 'RANDOM_PID', 'RANDOM_MAN', 'RANDOM_PRODUCT', 'SET_BOOT_SCRIPT'
]);



function getLevenshteinDistance(a, b) {
    if (!a || !b) return (a || b).length;
    const matrix = [];
    for (let i = 0; i <= b.length; i++) matrix[i] = [i];
    for (let j = 0; j <= a.length; j++) matrix[0][j] = j;
    for (let i = 1; i <= b.length; i++) {
        for (let j = 1; j <= a.length; j++) {
            if (b.charAt(i - 1) === a.charAt(j - 1)) matrix[i][j] = matrix[i - 1][j - 1];
            else matrix[i][j] = Math.min(matrix[i - 1][j - 1] + 1, Math.min(matrix[i][j - 1] + 1, matrix[i - 1][j] + 1));
        }
    }
    return matrix[b.length][a.length];
}

function getDidYouMean(cmd) {
    if (!cmd || cmd.length < 2) return null;
    let bestMatch = null;
    let bestDist = Infinity;
    for (const v of VALID_KEYWORDS) {
        const dist = getLevenshteinDistance(cmd, v);
        if (dist < bestDist && dist <= 3) {
            bestDist = dist;
            bestMatch = v;
        }
    }
    return bestMatch;
}

function fastUpdateHighlights() {
    const scriptArea = document.getElementById('scriptArea');
    const highlights = document.getElementById('editorHighlights');
    if (!scriptArea || !highlights) return;

    const lines = scriptArea.value.split('\n');
    
    globalDeclaredVars.clear();
    globalDeclaredFunctions.clear();

    lines.forEach((line) => {
        const trimmed = line.trim();
        const upper = trimmed.toUpperCase();
        
        const varDefMatch = trimmed.match(/^(VAR|VARIABLE)\s+([a-zA-Z0-9_]+)/i);
        if (varDefMatch) globalDeclaredVars.add(varDefMatch[2].toUpperCase());
        
        const varPrefixMatch = upper.match(/^(VAR_|VARIABLE_)([A-Z0-9_]*)\s*=/);
        if (varPrefixMatch) {
            const name = (varPrefixMatch[1] + varPrefixMatch[2]).toUpperCase();
            globalDeclaredVars.add(name);
        }

        const assignMatch = trimmed.match(/^([a-zA-Z_][a-zA-Z0-9_]*)\s*=/);
        if (assignMatch) {
            const name = assignMatch[1].toUpperCase();
            if (!VALID_KEYWORDS.has(name)) globalDeclaredVars.add(name);
        }

        const funcDefMatch = trimmed.match(/^(FUNCTION|DEF_)\s*([a-zA-Z0-9_]+)/i);
        if (funcDefMatch) globalDeclaredFunctions.add(funcDefMatch[2].toUpperCase());

        const forLoopMatch = trimmed.match(/^FOR\s+\$([a-zA-Z0-9_]+)/i);
        if (forLoopMatch) globalDeclaredVars.add(forLoopMatch[1].toUpperCase());
    });

    let html = '';
    for (let line of lines) {
        html += `<div>${applyHighlighting(line)}</div>`;
    }
    highlights.innerHTML = html;
}

function updateErrorLens() {
    try {
        const scriptArea = document.getElementById('scriptArea');
        const highlights = document.getElementById('editorHighlights');
        const container = document.getElementById('issuesList');
        const statusEl = document.getElementById('issueStatus');
        const panel = document.getElementById('issuesPanel');
        if (!scriptArea || !highlights) return;

        const lines = scriptArea.value.split('\n');
        
        globalDeclaredVars.clear();
        globalDeclaredFunctions.clear();

        const cursorIdx = scriptArea.selectionStart;
        const textBeforeCursor = scriptArea.value.substring(0, cursorIdx);
        const cursorLine = textBeforeCursor.split('\n').length - 1;

        const errors = [];
        let ifCount = 0, forCount = 0, whileCount = 0, repeatCount = 0;
        let inFunction = false;
        let linesInBlock = 0; // Track if function has content

        // Pre-scan for unclosed functions
        let lastUnclosedFunctionStart = -1;
        let funcLevel = 0;
        lines.forEach((line, idx) => {
            const trimmed = line.trim().toUpperCase();
            const isDefStart = trimmed.startsWith('FUNCTION') || trimmed.startsWith('DEF_') || (trimmed.endsWith('():') && !trimmed.startsWith('END_'));
            
            if (isDefStart) {
                lastUnclosedFunctionStart = idx;
                funcLevel++;
            } else if (trimmed === 'END_FUNCTION' || trimmed === 'END_DEF') {
                funcLevel--;
                if (funcLevel <= 0) {
                    lastUnclosedFunctionStart = -1;
                    funcLevel = 0;
                }
            }
        });

        const makeError = (txt) => ({ type: 'error', text: txt });
        const makeWarning = (txt, v = 'WIFI', idx) => ({ type: 'warning', text: txt, var: v, line: idx });

        const needsInput = [
            'STRING', 'STRINGLN', 'DELAY', 'DEFAULTDELAY', 'DEFAULT_DELAY', 'REPEAT', 
            'IF', 'ELIF', 'FOR', 'FUNCTION', 'DEF_', 'HOLD', 'KEYCODE',
            'DOWNLOAD_FILE', 'UPLOAD_FILE', 'JOIN_INTERNET', 'IF_PRESENT', 'IF_NOTPRESENT',
            'IF_BT_PRESENT', 'IF_OS', 'IF_DETECT_OS_INCLUDES', 'RUN_AT_TIME', 'RUN_AT_DAY',
            'WAIT_FOR_EVENT', 'RUN_WHEN_WIFI', 'LOCALE'
        ];

        // Pass 1: Collect definitions
        lines.forEach((line) => {
            const trimmed = line.trim();
            const upper = trimmed.toUpperCase();
            
            // Standard VAR/FUNCTION
            const varDefMatch = trimmed.match(/^(VAR|VARIABLE)\s+([a-zA-Z0-9_]+)/i);
            if (varDefMatch) globalDeclaredVars.add(varDefMatch[2].toUpperCase());
            
            const funcDefMatch = trimmed.match(/^(FUNCTION|DEF_)\s*([a-zA-Z0-9_]+)/i);
            if (funcDefMatch) globalDeclaredFunctions.add(funcDefMatch[2].toUpperCase());

            // Prefixed FUNCTION_ (Image 2 support)
            const funcPrefixMatch = upper.match(/^FUNCTION_([A-Z0-9_]+)/);
            if (funcPrefixMatch) globalDeclaredFunctions.add(funcPrefixMatch[1]);

            // Label-style FunctionName(): (Image 3 support)
            const funcLabelMatch = upper.match(/^([A-Z0-9_]+)\(\):/);
            if (funcLabelMatch) globalDeclaredFunctions.add(funcLabelMatch[1]);
            
            const varPrefixMatch = upper.match(/^(VAR_|VARIABLE_)([A-Z0-9_]*)\s*=/);
            if (varPrefixMatch) {
                const name = (varPrefixMatch[1] + varPrefixMatch[2]).toUpperCase();
                globalDeclaredVars.add(name);
            }

            const assignMatch = trimmed.match(/^([a-zA-Z_][a-zA-Z0-9_]*)\s*=/);
            if (assignMatch) {
                const name = assignMatch[1].toUpperCase();
                if (!VALID_KEYWORDS.has(name) && (name.startsWith('VAR_') || name.startsWith('VARIABLE_'))) {
                    globalDeclaredVars.add(name);
                }
            }

            const forLoopMatch = trimmed.match(/^FOR\s+\$([a-zA-Z0-9_]+)/i);
            if (forLoopMatch) globalDeclaredVars.add(forLoopMatch[1].toUpperCase());
        });

        // Pass 2: Validation
        const processedVars = new Set();
        const processedFunctions = new Set();
        let highlightsHTML = '';
        let blockBroken = false;

        lines.forEach((line, i) => {
            const trimmed = line.trim();
            const upper = trimmed.toUpperCase();
            const words = trimmed.split(/\s+/);
            let cmd = words[0].toUpperCase();
            if (cmd.endsWith(':')) cmd = cmd.slice(0, -1);
            const argStr = trimmed.substring(words[0].length).trim();
            let errorMsg = null;

            const isIndented = line.startsWith(' ') || line.startsWith('\t');
            const isDefStartLine = upper.startsWith('FUNCTION') || upper.startsWith('DEF_') || (upper.endsWith('():') && !upper.startsWith('END_'));

            // Check if block is broken - must run before internal state changes
            if (inFunction && !isIndented && !isDefStartLine && upper !== 'END_FUNCTION') {
                if (!trimmed || (!trimmed.startsWith('REM') && !trimmed.startsWith('//'))) {
                    blockBroken = true;
                }
            }

            if (trimmed && !trimmed.startsWith('REM') && !trimmed.startsWith('//')) {
                const varPrefixMatch = upper.match(/^(VAR_|VARIABLE_)([A-Z0-9_]+)/);
                if (upper.startsWith('VAR_') || upper.startsWith('VARIABLE_')) {
                    if (!varPrefixMatch) {
                        errorMsg = makeError("Variable name required after '_' (e.g., VAR_MYVAR)");
                    } else {
                        const fullVarName = (varPrefixMatch[1] + varPrefixMatch[2]).toUpperCase();
                        if (trimmed.includes('=')) {
                            const afterEquals = trimmed.split('=')[1].trim();
                            if (!afterEquals && !ignoredWarnings.has(`${i}-${fullVarName}`)) {
                                errorMsg = makeWarning(`Variable assignment is empty`, fullVarName, i);
                            } else if (STRUCTURAL_KEYWORDS.has(afterEquals.toUpperCase())) {
                                errorMsg = makeError(`Variable assignment cannot be a structural keyword: '${afterEquals}'`);
                            } else if (processedVars.has(fullVarName)) {
                                errorMsg = makeError(`Variable '${fullVarName}' is already declared.`);
                            }
                            processedVars.add(fullVarName);
                        } else {
                            errorMsg = makeError(`Variable declaration must use '=' (e.g., VAR_NAME = VALUE)`);
                        }
                    }
                } else if (upper.startsWith('VAR ') || upper.startsWith('VARIABLE ')) {
                    errorMsg = makeError("Legacy 'VAR' command is disabled. Use 'VAR_name = value' instead.");
                } else {
                    const assignMatch = trimmed.match(/^([a-zA-Z0-9_]+)\s*=/);
                    if (assignMatch) {
                        const name = assignMatch[1].toUpperCase();
                        if (!name.startsWith('VAR_') && !VALID_KEYWORDS.has(name)) {
                            errorMsg = makeError("Variables must start with the 'VAR_' prefix.");
                        }
                    } else if (trimmed && !trimmed.startsWith('REM') && !trimmed.startsWith('//') && !trimmed.endsWith('():') && !trimmed.startsWith('FUNCTION') && !trimmed.startsWith('DEF_')) {
                        if (inFunction) linesInBlock++;
                    }
                }

                if (!errorMsg && needsInput.includes(cmd) && !argStr) {
                    errorMsg = makeError(`${cmd} requires parameters or input`);
                }

                if (!errorMsg && !varPrefixMatch) {
                    const varDefMatch = trimmed.match(/^(VAR|VARIABLE)\s+([a-zA-Z0-9_]+)/i);
                    const funcDefMatch = trimmed.match(/^(FUNCTION|DEF_)\s*([a-zA-Z0-9_]+)/i);
                    const funcPrefixMatch = upper.match(/^FUNCTION_([A-Z0-9_]+)/);
                    const funcLabelMatch = upper.match(/^([A-Z0-9_]+)\(\):/);

                    if (varDefMatch) {
                        errorMsg = makeError("Legacy 'VAR' command is disabled. Use the 'VAR_name = value' format.");
                    } else if (funcDefMatch || funcPrefixMatch || funcLabelMatch) {
                        const name = (funcDefMatch ? funcDefMatch[2] : (funcPrefixMatch ? funcPrefixMatch[1] : funcLabelMatch[1])).toUpperCase();
                        if (processedFunctions.has(name)) errorMsg = makeError(`Function '${name}' is already defined.`);
                        processedFunctions.add(name);
                        inFunction = true;
                        linesInBlock = 0;
                    } else if (upper.startsWith('END_FUNCTION') || upper.startsWith('END_DEF')) {
                        if (!inFunction) {
                            errorMsg = makeError("Found 'END_FUNCTION' without a matching 'FUNCTION' block.");
                        } else {
                            if (upper !== 'END_FUNCTION' && upper !== 'END_DEF') {
                                const extra = trimmed.substring(cmd.length).trim();
                                errorMsg = makeError(`${cmd} needs to be in a newline. You still have text: '${extra}'`);
                            } else if (linesInBlock === 0 && !ignoredWarnings.has(`${i}-EMPTY_FUNC`)) {
                                errorMsg = makeWarning("Function has nothing inside of it", "EMPTY_FUNC", i);
                            }
                        }
                        inFunction = false;
                    } else if (upper === 'IF' || upper === 'IF:' || upper.startsWith('IF ') || upper.startsWith('IF_') || upper === 'RUN_ON_REBOOT' || upper === 'RUN_ON_REBOOT:' || upper.startsWith('RUN_ON_REBOOT ')) {
                        ifCount++;
                    } else if (upper.startsWith('ENDIF') || upper.startsWith('END_IF') || upper.startsWith('END_RUN_ON_REBOOT')) {
                        ifCount--;
                        if (ifCount < 0) { errorMsg = makeError(`Found '${cmd}' without a matching block.`); ifCount = 0; }
                        else if (upper !== 'ENDIF' && upper !== 'END_IF' && upper !== 'END_RUN_ON_REBOOT') {
                            const extra = trimmed.substring(cmd.length).trim();
                            errorMsg = makeError(`${cmd} needs to be in a newline. You still have text: '${extra}'`);
                        }
                    } else if (upper.startsWith('ELSE')) {
                        if (ifCount <= 0) {
                            errorMsg = makeError("Found 'ELSE' without a matching 'IF' block.");
                        } else if (upper !== 'ELSE') {
                            const extra = trimmed.substring(cmd.length).trim();
                            errorMsg = makeError(`ELSE needs to be in a newline. You still have text: '${extra}'`);
                        }
                    } else if (upper === 'FOR' || upper === 'FOR:' || upper.startsWith('FOR ')) {
                        forCount++;
                    } else if (upper.startsWith('ENDFOR') || upper.startsWith('END_FOR')) {
                        forCount--;
                        if (forCount < 0) { errorMsg = makeError(`Found '${cmd}' without a matching 'FOR' block.`); forCount = 0; }
                        else if (upper !== 'ENDFOR' && upper !== 'END_FOR') {
                            const extra = trimmed.substring(cmd.length).trim();
                            errorMsg = makeError(`${cmd} needs to be in a newline. You still have text: '${extra}'`);
                        }
                    } else if (upper === 'WHILE' || upper === 'WHILE:' || upper.startsWith('WHILE ')) {
                        whileCount++;
                    } else if (upper.startsWith('END_WHILE')) {
                        whileCount--;
                        if (whileCount < 0) { errorMsg = makeError(`Found 'END_WHILE' without a matching 'WHILE' block.`); whileCount = 0; }
                        else if (upper !== 'END_WHILE') {
                            const extra = trimmed.substring(cmd.length).trim();
                            errorMsg = makeError(`END_WHILE needs to be in a newline. You still have text: '${extra}'`);
                        }
                    } else if (upper === 'REPEAT' || upper === 'REPEAT:' || upper.startsWith('REPEAT ')) {
                        repeatCount++;
                        if (i === 0) errorMsg = makeError("REPEAT cannot be on the first line.");
                        const val = parseInt(argStr);
                        if (argStr && isNaN(val)) errorMsg = makeError("REPEAT requires a number");
                        else if (val > 100 && !ignoredWarnings.has(`${i}-REPEAT_HIGH`)) errorMsg = makeWarning("High repeat count. Payload will take time.", 'REPEAT_HIGH', i);
                    } else if (upper.startsWith('END_REPEAT')) {
                        repeatCount--;
                        if (repeatCount < 0) { errorMsg = makeError(`Found 'END_REPEAT' without a matching 'REPEAT' block.`); repeatCount = 0; }
                        else if (upper !== 'END_REPEAT') {
                            const extra = trimmed.substring(cmd.length).trim();
                            errorMsg = makeError(`END_REPEAT needs to be in a newline. You still have text: '${extra}'`);
                        }
                    } else if (cmd === 'DELAY' || cmd === 'DEFAULTDELAY' || cmd === 'DEFAULT_DELAY') {
                        const val = parseInt(argStr);
                        if (isNaN(val)) errorMsg = makeError(`${cmd} requires a number`);
                        else if (val < 0) errorMsg = makeError(`${cmd} cannot be negative`);
                        else if (val > 30000 && !ignoredWarnings.has(`${i}-DELAY_HIGH`)) errorMsg = makeWarning(`Delay is very long (${val}ms).`, 'DELAY_HIGH', i);
                        else if (val < 20 && val > 0 && !ignoredWarnings.has(`${i}-DELAY_FAST`)) errorMsg = makeWarning(`Delay might be too fast for some HID interfaces.`, 'DELAY_FAST', i);
                    } else if (cmd === 'LED') {
                        const parts = argStr.split(/\s+/).filter(x => x.length > 0);
                        if (parts.length === 3) {
                            parts.forEach(p => {
                                const v = parseInt(p);
                                if (isNaN(v) || v < 0 || v > 255) errorMsg = makeError("LED RGB values must be 0-255");
                            });
                        }
                    } else if (cmd === 'SELFDESTRUCT') {
                        if (!ignoredWarnings.has(`${i}-DANGER`)) errorMsg = makeWarning(`Dangerous command: This will trigger a device event immediately.`, 'DANGER', i);
                    } else if (trimmed.includes('=') && !upper.startsWith('IF') && !upper.startsWith('ELIF') && !upper.startsWith('FOR') && !upper.startsWith('WHILE')) {
                        const name = trimmed.split('=')[0].trim().toUpperCase();
                        const afterEquals = trimmed.split('=')[1].trim();
                        if (!afterEquals && !ignoredWarnings.has(`${i}-${name}`)) {
                            errorMsg = makeWarning(`Variable assignment is empty`, name, i);
                        } else if (STRUCTURAL_KEYWORDS.has(afterEquals.toUpperCase())) {
                            errorMsg = makeError(`Variable assignment cannot be a structural keyword: '${afterEquals}'`);
                        } else if (processedVars.has(name)) {
                             errorMsg = makeError(`Variable '${name}' is already declared.`);
                        } else if (!globalDeclaredVars.has(name) && !VALID_KEYWORDS.has(name)) {
                            errorMsg = makeError(`Variable '${name}' is used before being declared.`);
                        }
                        processedVars.add(name);
                    } else if (trimmed.endsWith('()')) {
                        const fName = trimmed.replace('()', '').trim().toUpperCase();
                        if (!globalDeclaredFunctions.has(fName)) errorMsg = makeError(`Call to undefined function: '${fName}()'`);
                    } else {
                        // FALLBACK: Unknown command or variable check
                        const isPrefixCmd = /^(VID_|PID_|MAN_|PRODUCT_|HOLD_|HOLD_TILL_)/.test(cmd);
                        const isKeyword = VALID_KEYWORDS.has(cmd);
                        const isDeclaredVar = globalDeclaredVars.has(cmd);

                        if (!isKeyword && !isDeclaredVar && !isPrefixCmd) {
                            if (cmd.endsWith(':')) {
                                const name = cmd.slice(0, -1);
                                errorMsg = makeError(`Unknown command '${cmd}'. Did you mean 'FUNCTION ${name}'?`);
                            } else {
                                const suggestion = getDidYouMean(cmd);
                                if (suggestion) {
                                    errorMsg = makeError(`Unknown command '${cmd}'. Did you mean '${suggestion}'?`);
                                } else {
                                    errorMsg = makeError(`Unknown command or variable: '${words[0]}'`);
                                }
                            }
                        }
                    }
                    
                    if (inFunction && !isDefStartLine && upper !== 'END_FUNCTION' && trimmed && !trimmed.startsWith('REM') && !trimmed.startsWith('//')) {
                        linesInBlock++;
                    }
                }
            }

                if (!errorMsg && ['STRING', 'STRINGLN', 'IF', 'ELIF', 'FOR'].includes(cmd)) {
                    const varMatches = trimmed.match(/(VAR_[a-zA-Z0-9_]*|VARIABLE_[a-zA-Z0-9_]*|\$[a-zA-Z0-9_]+)/gi);
                    if (varMatches) {
                        for (const m of varMatches) {
                            let v = m.toUpperCase();
                            // If it starts with $, strip it before checking if it was declared
                            if (v.startsWith('$')) v = v.substring(1);
                            
                            if (!globalDeclaredVars.has(v) && !ignoredWarnings.has(`${i}-${v}`)) {
                                errorMsg = makeWarning(`Trying to use variable '${v}'? It doesn't exist.`, v, i);
                                break;
                            }
                        }
                    }
                }

            const hlLine = applyHighlighting(line);
            
            // Check for block-level error (Unclosed Function)
            let isBlockErrorLine = (lastUnclosedFunctionStart !== -1 && i >= lastUnclosedFunctionStart && cursorLine > lastUnclosedFunctionStart && !blockBroken);
            let blockError = (isBlockErrorLine && i === lastUnclosedFunctionStart) ? "Block requires an END_FUNCTION" : null;

            if (errorMsg || isBlockErrorLine) {
                const isWarning = errorMsg ? errorMsg.type === 'warning' : false;
                const msgText = errorMsg ? (errorMsg.text || (typeof errorMsg === 'string' ? errorMsg : 'Unknown Issue')) : (blockError || "");
                const className = isWarning ? 'warning-line' : 'error-line';
                const lensClass = isWarning ? 'inline-warning' : 'inline-error';
                
                const isFixable = !isWarning && (msgText && (msgText.includes("Did you mean") || msgText.includes("END_FUNCTION")));
                // Only push to error list if it's the first line of block error or a normal error
                if (errorMsg || i === lastUnclosedFunctionStart) {
                    errors.push({ type: isWarning ? 'warning' : 'error', line: i, text: `Line ${i+1}: ${msgText}`, var: errorMsg ? errorMsg.var : null, fixable: isFixable });
                }
                
                const isStructuralStart = upper.startsWith('FUNCTION') || upper.startsWith('DEF_') || (upper.endsWith('():') && !upper.startsWith('END_')) || upper.startsWith('IF') || upper.startsWith('FOR') || upper.startsWith('WHILE') || upper.startsWith('REPEAT');
                const isStructuralEnd = upper === 'END_FUNCTION' || upper === 'END_DEF' || upper === 'ENDIF' || upper === 'END_IF' || upper === 'ENDFOR' || upper === 'END_FOR' || upper === 'END_WHILE' || upper === 'END_REPEAT' || upper === 'ELSE';
                
                const isScoped = (inFunction || ifCount > 0 || forCount > 0 || whileCount > 0 || repeatCount > 0) && !isStructuralStart && !isStructuralEnd && (line.startsWith(' ') || line.startsWith('\t'));
                const scopeClass = isScoped ? `scope-guide ${isBlockErrorLine ? 'scope-error' : ''}` : '';
                let lineContent = `<div class="${className} ${scopeClass}" style="position: relative; white-space: pre;">`;
                if (isWarning) {
                    lineContent += `<span class="warning-text">${hlLine}</span><span class="${lensClass}" title="${msgText.replace(/"/g, '&quot;')}">${msgText}</span>`;
                    lineContent += `<button class="ignore-btn-inline" style="pointer-events: auto;" onclick="event.stopPropagation(); ignoreWarning(${i}, '${errorMsg ? (errorMsg.var || '') : ''}', this)">Ignore</button>`;
                } else {
                    lineContent += `<span>${hlLine}</span><span class="${lensClass}" title="${msgText.replace(/"/g, '&quot;')}">${msgText}</span>`;
                }
                lineContent += `</div>`;
                highlightsHTML += lineContent;
            } else {
                const isStructuralStart = upper.startsWith('FUNCTION') || upper.startsWith('DEF_') || (upper.endsWith('():') && !upper.startsWith('END_')) || upper.startsWith('IF') || upper.startsWith('FOR') || upper.startsWith('WHILE') || upper.startsWith('REPEAT');
                const isStructuralEnd = upper === 'END_FUNCTION' || upper === 'END_DEF' || upper === 'ENDIF' || upper === 'END_IF' || upper === 'ENDFOR' || upper === 'END_FOR' || upper === 'END_WHILE' || upper === 'END_REPEAT';
                
                const isScoped = (inFunction || ifCount > 0 || forCount > 0) && !isStructuralStart && !isStructuralEnd && (line.startsWith(' ') || line.startsWith('\t'));
                const scopeClass = isScoped ? `scope-guide ${isBlockErrorLine ? 'scope-error' : ''}` : '';
                highlightsHTML += `<div class="${scopeClass}">${hlLine}</div>`;
            }
        });

        if (ifCount > 0) errors.push({ type: 'error', line: -1, text: "Missing 'ENDIF' for one or more 'IF' blocks." });
        if (forCount > 0) errors.push({ type: 'error', line: -1, text: "Missing 'ENDFOR' for one or more 'FOR' loops." });
        if (inFunction) errors.push({ type: 'error', line: -1, text: "Missing 'END_FUNCTION' for function definition.", fixable: true });

        highlights.innerHTML = highlightsHTML;
        
        if (container && panel) {
            const newState = JSON.stringify(errors);
            if (newState !== lastErrorState) {
                container.innerHTML = errors.map(e => {
                    const isError = e.type === 'error';
                    const itemClass = isError ? 'error-item' : 'warning-item';
                    const icon = isError ? '✕' : '▲';
                    return `
                        <div class="issue-item ${itemClass}" onclick="jumpToLine(${e.line})">
                            <div class="issue-icon">${icon}</div>
                            <div class="issue-text">${e.text}</div>
                        </div>`;
                }).join('');
                lastErrorState = newState;
            }

            const hasErrors = errors.some(e => e.type === 'error');
            const hasWarnings = errors.some(e => e.type === 'warning');

            if (errors.length > 0) {
                panel.style.display = 'block';
                panel.classList.toggle('has-errors', hasErrors);
                panel.classList.toggle('has-warnings', hasWarnings && !hasErrors);
                
                const countText = `${errors.length} Issue${errors.length > 1 ? 's' : ''} Found`;
                statusEl.innerHTML = `<span>${hasErrors ? '✕' : '▲'}</span> ${countText}`;
            } else {
                panel.style.display = 'none';
                panel.classList.remove('has-errors', 'has-warnings', 'expanded');
            }
        }

        const fixAllBtn = document.getElementById('fixAllBtn');
        if (fixAllBtn) {
            const hasFixable = errors.some(e => e.fixable);
            fixAllBtn.style.display = hasFixable ? 'block' : 'none';
        }
    } catch (e) { console.error("Validator Crash:", e); }
}

function jumpToLine(lineIdx) {
    if (lineIdx < 0) return;
    const scriptArea = document.getElementById('scriptArea');
    const lines = scriptArea.value.split('\n');
    let charPos = 0;
    for (let i = 0; i < lineIdx; i++) {
        charPos += lines[i].length + 1;
    }
    scriptArea.focus();
    scriptArea.setSelectionRange(charPos, charPos + lines[lineIdx].length);
    const lineHeight = 22;
    scriptArea.scrollTop = (lineIdx * lineHeight) - (scriptArea.offsetHeight / 3);
    syncHighlightsScroll();
}



function fixAllErrors() {
    const scriptArea = document.getElementById('scriptArea');
    let lines = scriptArea.value.split('\n');
    let changed = false;
    let newLines = [];
    let inFunc = false;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const trimmed = line.trim();
        const upper = trimmed.toUpperCase();
        
        // Block detection
        const isDefStart = upper.startsWith('FUNCTION') || upper.startsWith('DEF_') || (upper.endsWith('():') && !upper.startsWith('END_'));
        const isEnd = upper === 'END_FUNCTION' || upper === 'END_DEF';

        // If we are in a function and find a non-indented line that isn't a comment/empty, close it
        if (inFunc && !isEnd && trimmed && !line.startsWith(' ') && !line.startsWith('\t') && !isDefStart && !trimmed.startsWith('REM') && !trimmed.startsWith('//')) {
            newLines.push('END_FUNCTION');
            inFunc = false;
            changed = true;
        }

        if (isDefStart) inFunc = true;
        if (isEnd) inFunc = false;

        // Perform typo correction
        let fixedLine = line;
        if (trimmed && !trimmed.startsWith('REM') && !trimmed.startsWith('//') && !isDefStart && !isEnd) {
            const firstWord = trimmed.split(/\s+/)[0].toUpperCase().replace('()', '');
            if (!VALID_KEYWORDS.has(firstWord) && !globalDeclaredVars.has(firstWord) && !globalDeclaredFunctions.has(firstWord)) {
                let bestMatch = null;
                let minDistance = 3; 
                VALID_KEYWORDS.forEach(cmd => {
                    const dist = levenshtein(firstWord, cmd);
                    if (dist < minDistance) { minDistance = dist; bestMatch = cmd; }
                });
                if (bestMatch) {
                    fixedLine = line.replace(new RegExp(firstWord.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i'), bestMatch);
                    changed = true;
                }
            }
        }
        newLines.push(fixedLine);
    }

    if (inFunc) {
        newLines.push('END_FUNCTION');
        changed = true;
    }

    if (changed) {
        scriptArea.value = newLines.join('\n');
        updateGutter();
        updateErrorLens();
    }
}

function levenshtein(a, b) {
    const matrix = [];
    for (let i = 0; i <= b.length; i++) matrix[i] = [i];
    for (let j = 0; j <= a.length; j++) matrix[0][j] = j;
    for (let i = 1; i <= b.length; i++) {
        for (let j = 1; j <= a.length; j++) {
            if (b.charAt(i - 1) === a.charAt(j - 1)) matrix[i][j] = matrix[i - 1][j - 1];
            else matrix[i][j] = Math.min(matrix[i - 1][j - 1] + 1, matrix[i][j - 1] + 1, matrix[i - 1][j] + 1);
        }
    }
    return matrix[b.length][a.length];
}



function refreshFiles() {
    fetch('/api/scripts').then(r => r.json()).then(files => {
        const list = document.getElementById('fileList');
        list.innerHTML = '';
        files.forEach(f => {
            const item = document.createElement('div');
            item.className = 'file-item';
            item.innerHTML = `<span>${f}</span><div class="flex-row"><button onclick="loadFile('${f}')">Load</button><button class="danger" onclick="deleteFile('${f}')">Del</button></div>`;
            list.appendChild(item);
        });
    });
}

function refreshBootScripts() {
    fetch('/api/scripts').then(r => r.json()).then(files => {
        fetch('/api/stats').then(r => r.json()).then(stats => {
            const list = document.getElementById('bootScriptsList');
            list.innerHTML = '';
            files.forEach(f => {
                const checked = stats.bootScripts && stats.bootScripts.includes(f);
                const item = document.createElement('div');
                item.className = 'file-item';
                item.innerHTML = `<label class="custom-checkbox"><input type="checkbox" name="bootScript" value="${f}" ${checked ? 'checked' : ''}> ${f}</label>`;
                list.appendChild(item);
            });
        });
    });
}

function saveBootScripts() {
    const checkboxes = document.querySelectorAll('input[name="bootScript"]:checked');
    const filenames = Array.from(checkboxes).map(cb => cb.value);
    fetch('/api/set-boot-script', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ filenames: filenames })
    }).then(r => r.text()).then(msg => alert(msg));
}

function refreshFileBrowser() {
    const browser = document.getElementById('fileBrowser');
    browser.innerHTML = '<div class="file-browser-item">Loading...</div>';
    fetch('/api/list-files?path=' + encodeURIComponent(currentBrowserPath)).then(r => r.json()).then(files => {
        browser.innerHTML = '';
        document.getElementById('currentDirDisplay').textContent = currentBrowserPath;
        files.forEach(file => {
            const item = document.createElement('div');
            item.className = 'file-browser-item';
            item.innerHTML = `<span style="cursor:pointer; color:${file.isDirectory?'var(--primary)':'white'}" onclick="${file.isDirectory?`navigateToDirectory('${file.path}')`:`selectFileInBrowser(this, ${JSON.stringify(file)})`}">${file.name}${file.isDirectory?'/':''}</span><button class="danger" style="padding:2px 6px; font-size:10px;" onclick="deleteBrowserFile('${file.path}')">Del</button>`;
            browser.appendChild(item);
        });
    });
}

function executeScript() {
    const script = document.getElementById('scriptArea').value.trim();
    if (!script) return;
    const statusEl = document.getElementById('scriptStatus');
    statusEl.textContent = 'Executing...';
    fetch('/execute', { method: 'POST', body: script }).then(r => { statusEl.textContent = r.ok ? 'Script Finished' : 'Execution Failed'; });
}

function stopScript() { fetch('/stop', { method: 'POST' }); }
function clearScript() { document.getElementById('scriptArea').value = ''; updateGutter(); updateErrorLens(); scriptChanged = true; }
function loadFile(f) { fetch('/api/load?file='+encodeURIComponent(f)).then(r=>r.text()).then(c => { document.getElementById('scriptArea').value = c; openTab(null, 'Script'); updateGutter(); updateErrorLens(); scriptChanged = false; }); }
function deleteFile(f) { if(confirm('Delete?')) fetch('/api/delete?file='+encodeURIComponent(f), {method:'DELETE'}).then(()=>refreshFiles()); }
function saveScriptPrompt() { const n = prompt('Name:'); if(n) saveScriptAs(n); }
function saveScriptAs(n) { fetch('/api/save', {method:'POST', body:JSON.stringify({filename:n.endsWith('.txt')?n:n+'.txt', content:document.getElementById('scriptArea').value})}).then(()=>{scriptChanged = false; refreshFiles();}); }
function saveScript() { const n = document.getElementById('newFilename').value; if(n) saveScriptAs(n); }
function navigateToDirectory(path) { currentBrowserPath = path; refreshFileBrowser(); }
function goToParent() { if (currentBrowserPath === '/') return; const p = currentBrowserPath.split('/'); p.pop(); currentBrowserPath = p.join('/') || '/'; refreshFileBrowser(); }
function initFileManager() { document.getElementById('uploadArea').onclick = () => document.getElementById('fileInput').click(); document.getElementById('fileInput').onchange = (e) => Array.from(e.target.files).forEach(uploadFile); }
function uploadFile(file) {
    const form = new FormData();
    form.append('file', file, currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/upload');
    xhr.onload = () => refreshFileBrowser();
    xhr.send(form);
}
function updateStats() {
    const autoRetry = localStorage.getItem('autoRetryConn') !== 'false';
    const retryCheckbox = document.getElementById('retryConnToggle');
    if (retryCheckbox && !retryCheckbox.checked) return; // Respect popup checkbox

    if (statsController) statsController.abort();
    statsController = new AbortController();
    
    fetch('/api/stats', { signal: statsController.signal })
        .then(r => r.json())
        .then(data => {
        const set = (id, val) => { const el = document.getElementById(id); if (el) el.textContent = val; };
        set('errorCount', data.errorCount);
        set('totalScripts', data.totalScripts);
        set('totalCommands', data.totalCommands);
        set('clientCount', data.clientCount);
        set('detectedOS', data.detectedOS);
        set('uptime', data.uptime + 's');
        set('freeMemory', Math.round(data.freeMemory / 1024) + ' KB');
        set('lastError', data.lastError || 'None');

        // Update WiFi Status
        const wifiStatusEl = document.getElementById('wifiStatus');
        window.isWifiConnected = data.wifiConnected;
        if (wifiStatusEl) {
            if (data.wifiConnected) {
                const uiSSID = document.getElementById('wifiSSIDJoin').value;
                const isSynced = (uiSSID === data.staSSID);
                wifiStatusEl.textContent = `Connected: ${data.staSSID} ${isSynced ? '(Synced)' : '(Modified)'}`;
                wifiStatusEl.style.color = isSynced ? 'var(--primary)' : '#ff9800'; // Orange for modified
            } else {
                wifiStatusEl.textContent = 'Status: Idle';
                wifiStatusEl.style.color = 'var(--text-muted)';
            }
        }

        // Update toggles even if hidden
        // Synchronize Settings (Toggles)
        const toggles = {
            'ledToggle': data.ledEnabled,
            'loggingToggle': data.loggingEnabled,
            'btToggle': data.btToggleEnabled,
            'btDiscoveryToggle': data.btDiscoveryEnabled
        };
        for (const [id, val] of Object.entries(toggles)) {
            const el = document.getElementById(id);
            if (el) el.checked = (val === true);
        }

        // Sync new WiFi/connect toggles
        const autoConn = document.getElementById('autoConnectToggle');
        if (autoConn) autoConn.checked = (data.autoConnectEnabled === true);
        const saveCred = document.getElementById('saveCredToggle');
        if (saveCred) saveCred.checked = (data.saveOnConnectEnabled === true);

        // Auto-discover languages if not done yet
        if (!languagesDiscovered) {
            discoverLanguages(data.currentLanguage);
        } else {
            const select = document.getElementById('languageSelect');
            if (select && document.activeElement !== select) {
                const targetVal = (data.currentLanguage || 'us').toLowerCase();
                if (select.value !== targetVal) select.value = targetVal;
            }
        }
    }).catch(err => {
        if (err.name !== 'AbortError') {
            console.error("Stats Fetch Error:", err);
            showConnectionError();
        }
    });
}

function showConnectionError() {
    if (sessionStorage.getItem('hideConnectionError') === 'true') return;
    const el = document.getElementById('connectionError');
    if (el) el.style.display = 'flex';
}

function hideConnectionError() {
    const el = document.getElementById('connectionError');
    const checkbox = document.getElementById('dontShowConnError');
    if (checkbox && checkbox.checked) {
        sessionStorage.setItem('hideConnectionError', 'true');
    }
    if (el) el.style.display = 'none';
}

function retryConnection() {
    console.log("Retrying connection...");
    hideConnectionError();
    updateStats();
    // Also try to refresh files if we were in that tab
    if (typeof refreshFiles === 'function') refreshFiles();
}

let discoveryRetryCount = 0;
function discoverLanguages(currentLang) {
    console.log("Discovering languages...");
    fetch('/api/languages')
        .then(r => {
            if (!r.ok) throw new Error("HTTP " + r.status);
            return r.json();
        })
        .then(langs => {
            const select = document.getElementById('languageSelect');
            if (!select) return;
            
            if (langs.length > 0) {
                languagesDiscovered = true;
                select.innerHTML = '';
                langs.forEach(l => {
                    const opt = document.createElement('option');
                    opt.value = l.toLowerCase();
                    opt.textContent = l.toUpperCase();
                    select.appendChild(opt);
                });
                
                const target = (currentLang || 'us').toLowerCase();
                select.value = target;
            } else if (discoveryRetryCount < 3) {
                discoveryRetryCount++;
                setTimeout(() => discoverLanguages(currentLang), 2000);
            }
        }).catch(err => {
            console.error("Language discovery failed:", err);
            if (discoveryRetryCount < 3) {
                discoveryRetryCount++;
                setTimeout(() => discoverLanguages(currentLang), 2000);
            }
        });
}

function initSettingsTab() {
    fetch('/api/stats').then(r => r.json()).then(data => {
        // USB Settings
        document.getElementById('usbVID').value = data.usbVID || '';
        document.getElementById('usbPID').value = data.usbPID || '';
        document.getElementById('usbMfr').value = data.usbMfr || '';
        document.getElementById('usbProd').value = data.usbProd || '';
        document.getElementById('usbRndVID').checked = data.usbRndVID;
        document.getElementById('usbRndPID').checked = data.usbRndPID;
        
        // System Toggles
        document.getElementById('ledToggle').checked = (data.ledEnabled === true);
        document.getElementById('loggingToggle').checked = (data.loggingEnabled === true);
        document.getElementById('btToggle').checked = (data.btToggleEnabled === true);
        const btDisc = document.getElementById('btDiscoveryToggle');
        if (btDisc) btDisc.checked = (data.btDiscoveryEnabled === true);
        
        // AP Settings
        document.getElementById('wifiSSID').value = data.wifiSSID || '';
        document.getElementById('wifiPassword').value = data.wifiPassword || '';
        document.getElementById('wifiScanTime').value = data.wifiScanTime || 5000;
        
        handleRandomToggle();
    }).catch(err => {
        console.error("Settings Tab Init Failed:", err);
        alert("Failed to load settings from ESP32. Please check connection.\nError: " + err.message);
    });
}
function toggleLED() { 
    const enabled = document.getElementById('ledToggle').checked;
    fetch('/api/toggle-led', {
        method:'POST', 
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({enabled: enabled})
    }); 
}
function toggleLogging() { fetch('/api/toggle-logging', {method:'POST', body:JSON.stringify({enabled:document.getElementById('loggingToggle').checked})}); }
function toggleWiFi() { fetch('/api/toggle-wifi', {method:'POST', body:JSON.stringify({enabled:document.getElementById('wifiToggle').checked})}); }
function toggleBluetooth() { fetch('/api/toggle-bluetooth', {method:'POST', body:JSON.stringify({enabled:document.getElementById('btToggle').checked})}); }
function toggleBluetoothDiscovery() { fetch('/api/toggle-bt-discovery', {method:'POST', body:JSON.stringify({enabled:document.getElementById('btDiscoveryToggle').checked})}); }
function toggleAutoConnect() { fetch('/api/toggle-autoconnect', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({enabled:document.getElementById('autoConnectToggle').checked})}); }
function toggleSaveOnConnect() { fetch('/api/toggle-save-on-connect', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({enabled:document.getElementById('saveCredToggle').checked})}); }

function toggleSavedNetworksPanel() {
    const panel = document.getElementById('savedNetworksPanel');
    const isHidden = panel.style.display === 'none' || !panel.style.display;
    panel.style.display = isHidden ? 'block' : 'none';
    if (isHidden) loadSavedNetworks();
}

function loadSavedNetworks() {
    const list = document.getElementById('savedNetworksList');
    if (list) list.innerHTML = 'Loading...';
    fetch('/api/saved-wifi').then(r => r.json()).then(networks => {
        if (!list) return;
        if (!networks || networks.length === 0) {
            list.innerHTML = '<div style="color:var(--text-muted); padding:8px;">No saved networks</div>';
            return;
        }
        list.innerHTML = '';
        networks.forEach(net => {
            const item = document.createElement('div');
            item.className = 'file-item';
            item.innerHTML = `<span style="flex:1">${net.ssid}</span><button onclick="connectToSaved('${net.ssid}','${net.pass}')" style="font-size:11px;padding:3px 8px;">Connect</button><button class="danger" onclick="deleteSavedNetwork('${net.ssid}')" style="font-size:11px;padding:3px 8px;">✕</button>`;
            list.appendChild(item);
        });
    }).catch(() => { if (list) list.innerHTML = 'Failed to load'; });
}

function connectToSaved(ssid, password) {
    const statusEl = document.getElementById('wifiStatus');
    if (statusEl) statusEl.textContent = 'Connecting to ' + ssid + '...';
    fetch('/api/join-internet', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ssid, password})
    }).then(r => r.json()).then(res => { if (res.status === 'connecting') pollWiFiJoinStatus(); });
}

function deleteSavedNetwork(ssid) {
    if (!confirm('Delete saved network: ' + ssid + '?')) return;
    fetch('/api/delete-saved-wifi', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ssid})
    }).then(() => loadSavedNetworks());
}

function quickSetBootScript() {
    const name = document.getElementById('quickBootScript').value.trim();
    if (!name) return alert('Enter a filename');
    const filename = name.endsWith('.txt') ? name : name + '.txt';
    fetch('/api/set-boot-script', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({filenames: [filename]})
    }).then(r => r.text()).then(msg => alert(msg));
}
function handleRandomToggle() {
    const rndVID = document.getElementById('usbRndVID').checked;
    const rndPID = document.getElementById('usbRndPID').checked;
    const vidInput = document.getElementById('usbVID');
    const pidInput = document.getElementById('usbPID');
    if (vidInput) { vidInput.disabled = rndVID; vidInput.style.opacity = rndVID ? '0.5' : '1'; }
    if (pidInput) { pidInput.disabled = rndPID; pidInput.style.opacity = rndPID ? '0.5' : '1'; }
}

function saveUSBSettings() {
    const data = { vid: document.getElementById('usbVID').value, pid: document.getElementById('usbPID').value, rndVid: document.getElementById('usbRndVID').checked, rndPid: document.getElementById('usbRndPID').checked, mfr: document.getElementById('usbMfr').value, prod: document.getElementById('usbProd').value };
    fetch('/api/save-usb', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(data)}).then(()=>alert('Rebooting...'));
}
function saveLanguageSettings() {
    const lang = document.getElementById('languageSelect').value;
    fetch('/api/save-settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ type: 'language', language: lang })
    }).then(r => r.text()).then(msg => alert(msg));
}

function saveWiFiSettings() {
    const data = {
        ssid: document.getElementById('wifiSSID').value,
        password: document.getElementById('wifiPassword').value,
        scanTime: parseInt(document.getElementById('wifiScanTime').value)
    };
    fetch('/api/save-wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    }).then(r => r.text()).then(msg => alert(msg));
}
function refreshTasks() {
    fetch('/api/tasks').then(r => r.json()).then(tasks => {
        const list = document.getElementById('taskList');
        if (!list) return;
        list.innerHTML = tasks.length ? '' : '<div class="file-item">No active background tasks</div>';
        tasks.forEach(t => {
            list.innerHTML += `<div class="file-item"><span>${t.description}</span><button class="danger" onclick="cancelTask(${t.id})">Cancel</button></div>`;
        });
        // Check for active delay via stats
        fetch('/api/stats').then(r => r.json()).then(data => {
            if (data.delayProgress > 0) {
                const secs = Math.round(data.delayTotal / 1000);
                list.innerHTML = `<div class="file-item" style="flex-direction:column; gap:6px;"><span>⏳ DELAY — ${secs}s total</span><div style="width:100%;height:6px;background:var(--glass-border);border-radius:3px;"><div style="height:100%;width:${data.delayProgress}%;background:var(--primary);border-radius:3px;transition:width 0.5s"></div></div></div>` + list.innerHTML;
            }
        }).catch(() => {});
    });
}
function cancelTask(id) { fetch('/api/cancel-task', {method:'POST', body:JSON.stringify({id})}).then(() => refreshTasks()); }
function toggleHelp(id) { const el = document.getElementById(id); el.style.display = el.style.display === 'none' ? 'block' : 'none'; }
function joinInternet() {
    const ssid = document.getElementById('wifiSSIDJoin').value;
    const password = document.getElementById('wifiPasswordJoin').value;
    if (!ssid) return alert('SSID required');
    
    const statusEl = document.getElementById('wifiStatus');
    if (statusEl) statusEl.textContent = 'Connecting...';
    
    fetch('/api/join-internet', { 
        method: 'POST', 
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid, password, save: document.getElementById('saveCredToggle')?.checked || false }) 
    }).then(r => r.json()).then(res => {
        if (res.status === 'connecting') {
            pollWiFiJoinStatus();
        } else {
            if (statusEl) statusEl.textContent = 'Failed to start connection';
        }
    });
}

function pollWiFiJoinStatus() {
    const statusEl = document.getElementById('wifiStatus');
    fetch('/api/wifi-join-status?t=' + Date.now()).then(r => r.json()).then(res => {
        if (res.status === 'connected') {
            statusEl.textContent = `Connected! IP: ${res.ip}`;
            statusEl.style.color = 'var(--primary)';
            updateStats(); // Refresh everything
        } else if (res.status === 'connecting') {
            statusEl.textContent = 'Still connecting...';
            setTimeout(pollWiFiJoinStatus, 2000);
        } else {
            statusEl.textContent = 'Connection Failed / Idle';
            statusEl.style.color = 'var(--danger)';
        }
    });
}

function leaveInternet() {
    fetch('/api/leave-internet', { method: 'POST' }).then(() => {
        const statusEl = document.getElementById('wifiStatus');
        if (statusEl) {
            statusEl.textContent = 'Disconnected';
            statusEl.style.color = 'var(--text-muted)';
        }
        updateStats();
    });
}

function scanNetworksForUI(btn) {
    btn.disabled = true;
    btn.textContent = 'Starting Scan...';
    
    fetch('/api/scan-wifi').then(r => r.json()).then(res => {
        if (res.status === 'scanning') {
            btn.textContent = 'Scanning...';
            pollScanResults(btn);
        } else {
            btn.textContent = 'Scan Failed';
            btn.disabled = false;
        }
    }).catch(() => {
        btn.textContent = 'Scan WiFis';
    }, 1000);
}

// =========================================================
// Autocomplete System
// =========================================================
let autocompletePopup = null;
let autocompleteSuggestions = [];
let autocompleteIndex = -1;

function initAutocomplete() {
    autocompletePopup = document.createElement('div');
    autocompletePopup.id = 'autocompletePopup';
    autocompletePopup.className = 'autocomplete-popup';
    document.body.appendChild(autocompletePopup);

    const scriptArea = document.getElementById('scriptArea');
    if (!scriptArea) return;

    scriptArea.addEventListener('input', handleAutocompleteInput);
    scriptArea.addEventListener('keydown', handleAutocompleteKeydown);
    
    document.addEventListener('click', (e) => {
        if (e.target !== scriptArea && !autocompletePopup.contains(e.target)) {
            hideAutocomplete();
        }
    });
}

function handleAutocompleteInput(e) {
    const scriptArea = e.target;
    const text = scriptArea.value;
    const cursor = scriptArea.selectionStart;
    
    const lastNewline = text.lastIndexOf('\n', cursor - 1);
    const lineStart = lastNewline === -1 ? 0 : lastNewline + 1;
    const currentLine = text.substring(lineStart, cursor);
    
    const trimmed = currentLine.trimStart();
    if (trimmed.includes(' ')) {
        hideAutocomplete();
        return;
    }

    const word = trimmed.toUpperCase();
    if (word.length < 1) {
        hideAutocomplete();
        return;
    }

    // Dynamic scan for fresh suggestions
    const currentVars = new Set();
    const currentFuncs = new Set();
    text.split('\n').forEach(l => {
        const t = l.trim();
        const u = t.toUpperCase();
        const vM = t.match(/^(VAR|VARIABLE)\s+([a-zA-Z0-9_]+)/i);
        if (vM) currentVars.add(vM[2].toUpperCase());
        const vPM = u.match(/^(VAR_|VARIABLE_)([A-Z0-9_]+)/);
        if (vPM) currentVars.add((vPM[1] + vPM[2]).toUpperCase());
        const fM = t.match(/^(FUNCTION|DEF_)\s*([a-zA-Z0-9_]+)/i);
        if (fM) currentFuncs.add(fM[2].toUpperCase());
        const fPM = u.match(/^FUNCTION_([A-Z0-9_]+)/);
        if (fPM) currentFuncs.add(fPM[1]);
        const fLM = u.match(/^([A-Z0-9_]+)\(\):/);
        if (fLM) currentFuncs.add(fLM[1]);
    });

    const dynamicCommands = [
        ...Array.from(VALID_KEYWORDS), 
        ...Array.from(currentVars),
        ...Array.from(currentFuncs)
    ];

    autocompleteSuggestions = dynamicCommands.filter(c => c.startsWith(word) && c !== word);

    if (autocompleteSuggestions.length > 0) {
        showAutocomplete(scriptArea);
    } else {
        hideAutocomplete();
    }
}

function showAutocomplete(scriptArea) {
    const setting = document.getElementById('settingAutocomplete');
    if (setting && !setting.checked) return;

    // Elite Header
    autocompletePopup.innerHTML = '<div class="autocomplete-header">Suggestions</div>';
    const listContainer = document.createElement('div');
    listContainer.className = 'autocomplete-list';
    
    autocompleteSuggestions.forEach((sug, i) => {
        const item = document.createElement('div');
        item.className = 'autocomplete-item';
        item.textContent = sug;
        item.onmousedown = (e) => {
            e.preventDefault();
            const cursor = scriptArea.selectionStart;
            const lastNewline = scriptArea.value.lastIndexOf('\n', cursor - 1);
            const lineStart = lastNewline === -1 ? 0 : lastNewline + 1;
            applyAutocomplete(sug, scriptArea, lineStart, cursor);
        };
        listContainer.appendChild(item);
    });
    autocompletePopup.appendChild(listContainer);

    autocompleteIndex = 0;
    highlightAutocompleteItem();

    // Position FLOATING after the cursor
    const editorWrapper = scriptArea.closest('.editor-wrapper');
    if (editorWrapper) {
        const wrapperRect = editorWrapper.getBoundingClientRect();
        const cursorIdx = scriptArea.selectionStart;
        const textBeforeCursor = scriptArea.value.substr(0, cursorIdx);
        const lines = textBeforeCursor.split('\n');
        const currentLineIdx = lines.length - 1;
        const currentLineText = lines[currentLineIdx];
        
        // Calculate X offset based on text width + margin (3 chars approx 24px)
        const charWidth = 8.4; // Average for Consolas 14px
        const textWidth = getTextWidth(currentLineText, "14px Consolas");
        const xOffset = 15 + 40 + textWidth + (charWidth * 3); // Padding + Gutter + Width + 3 chars
        
        // Vertical position based on current line (22px height)
        const topOffset = wrapperRect.top + (currentLineIdx * 22) + 15;
        
        // Final bounds check to stay inside editor
        const leftPos = Math.min(wrapperRect.right - 200, wrapperRect.left + xOffset);
        
        autocompletePopup.style.top = `${topOffset}px`;
        autocompletePopup.style.left = `${leftPos}px`;
        autocompletePopup.style.right = 'auto';
        autocompletePopup.style.display = 'block';
    }
}

// Helper to calculate text width for precision positioning
function getTextWidth(text, font) {
    const canvas = getTextWidth.canvas || (getTextWidth.canvas = document.createElement("canvas"));
    const context = canvas.getContext("2d");
    context.font = font;
    const metrics = context.measureText(text);
    return metrics.width;
}

function hideAutocomplete() {
    if (autocompletePopup) {
        autocompletePopup.style.display = 'none';
        autocompleteSuggestions = [];
        autocompleteIndex = -1;
    }
}

function highlightAutocompleteItem() {
    const items = autocompletePopup.getElementsByClassName('autocomplete-item');
    for (let i = 0; i < items.length; i++) {
        if (i === autocompleteIndex) {
            items[i].classList.add('selected');
            items[i].scrollIntoView({ block: 'nearest' });
        } else {
            items[i].classList.remove('selected');
        }
    }
}

function handleAutocompleteKeydown(e) {
    if (autocompletePopup && autocompletePopup.style.display === 'block') {
        if (e.key === 'ArrowDown') {
            e.preventDefault();
            autocompleteIndex = (autocompleteIndex + 1) % autocompleteSuggestions.length;
            highlightAutocompleteItem();
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            autocompleteIndex = (autocompleteIndex - 1 + autocompleteSuggestions.length) % autocompleteSuggestions.length;
            highlightAutocompleteItem();
        } else if (e.key === 'Enter' || e.key === 'Tab') {
            e.preventDefault();
            if (autocompleteIndex >= 0 && autocompleteIndex < autocompleteSuggestions.length) {
                const scriptArea = e.target;
                const cursor = scriptArea.selectionStart;
                const lastNewline = scriptArea.value.lastIndexOf('\n', cursor - 1);
                const lineStart = lastNewline === -1 ? 0 : lastNewline + 1;
                applyAutocomplete(autocompleteSuggestions[autocompleteIndex], scriptArea, lineStart, cursor);
            }
        } else if (e.key === 'Escape') {
            hideAutocomplete();
        }
    }
}

function applyAutocomplete(suggestion, scriptArea, lineStart, cursor) {
    const text = scriptArea.value;
    const before = text.substring(0, lineStart);
    const after = text.substring(cursor);
    
    scriptArea.value = before + suggestion + ' ' + after;
    scriptArea.selectionStart = scriptArea.selectionEnd = lineStart + suggestion.length + 1;
    hideAutocomplete();
    updateErrorLens();
    const hl = document.getElementById('editorHighlights');
    if (hl) {
        hl.innerHTML = scriptArea.value.split('\n').map(applyHighlighting).join('\n');
    }
}




function syncHighlightsScroll() {
    const scriptArea = document.getElementById('scriptArea');
    const highlights = document.getElementById('editorHighlights');
    const gutter = document.getElementById('editorGutter');
    if (scriptArea && highlights) {
        highlights.scrollTop = scriptArea.scrollTop;
        highlights.scrollLeft = scriptArea.scrollLeft;
    }
    if (scriptArea && gutter) {
        gutter.scrollTop = scriptArea.scrollTop;
    }
}

function setupCustomScrollbar(contentId, trackId, thumbId) {
    const content = document.getElementById(contentId);
    const track = document.getElementById(trackId);
    const thumb = document.getElementById(thumbId);
    
    if (!content || !track || !thumb) return;

    let isDragging = false;
    let startY, startScrollTop;

    function updateThumb() {
        const height = content.clientHeight;
        const scrollHeight = content.scrollHeight;
        const scrollTop = content.scrollTop;
        const trackHeight = track.clientHeight;
        
        // Ensure we have a valid height before calculating
        if (height <= 0 || trackHeight <= 0) {
            setTimeout(updateThumb, 50);
            return;
        }

        track.style.display = 'block';

        const contentIndicator = document.getElementById(contentId === 'scriptArea' ? 'customScrollbarContent' : '');
        if (contentIndicator) {
            if (contentId === 'scriptArea' && content.value === "") {
                contentIndicator.style.height = `0%`;
            } else if (contentId === 'scriptArea') {
                const lineCount = content.value.split('\n').length;
                // 22px per line + 30px padding (15 top, 15 bottom)
                const actualTextHeight = (lineCount * 22) + 30;
                const coverage = Math.min(1, actualTextHeight / height);
                contentIndicator.style.height = `${coverage * 100}%`;
            } else {
                // For other lists like file browser
                const coverage = Math.min(1, scrollHeight / height);
                contentIndicator.style.height = `${coverage * 100}%`;
            }
        }

        const isTiny = (contentId === 'scriptArea');
        const thumbHeight = isTiny ? 45 : Math.max(45, (height / Math.max(scrollHeight, 1)) * height);
        
        const scrollRange = Math.max(0, scrollHeight - height);
        const thumbRange = trackHeight - thumbHeight;
        
        let thumbTop = 0;
        if (scrollRange > 0) {
            thumbTop = (scrollTop / scrollRange) * thumbRange;
        }
        
        thumb.style.height = `${thumbHeight}px`;
        thumb.style.transform = `translateY(${thumbTop}px)`;
        
        if (contentId === 'scriptArea') {
            syncHighlightsScroll();
            const lines = content.value.split('\n').length;
            const counter = document.getElementById('lineCounter');
            if (counter) counter.textContent = `${lines} Line${lines !== 1 ? 's' : ''}`;
        }
    }

    content.addEventListener('scroll', updateThumb);
    window.addEventListener('resize', updateThumb);

    thumb.addEventListener('mousedown', (e) => {
        isDragging = true;
        startY = e.clientY;
        startScrollTop = content.scrollTop;
        thumb.classList.add('active');
        document.body.style.userSelect = 'none';
        e.preventDefault();
    });

    document.addEventListener('mousemove', (e) => {
        if (!isDragging) return;
        const deltaY = e.clientY - startY;
        const height = content.clientHeight;
        const scrollHeight = content.scrollHeight;
        const thumbHeight = parseFloat(thumb.style.height);
        const scrollRange = scrollHeight - height;
        const thumbRange = height - thumbHeight;
        const scrollDelta = (deltaY / thumbRange) * scrollRange;
        content.scrollTop = startScrollTop + scrollDelta;
    });

    document.addEventListener('mouseup', () => {
        isDragging = false;
        thumb.classList.remove('active');
        document.body.style.userSelect = '';
    });

    track.addEventListener('mousedown', (e) => {
        if (e.target === thumb) return;
        const rect = track.getBoundingClientRect();
        const clickY = e.clientY - rect.top;
        const targetScrollTop = (clickY / rect.height) * content.scrollHeight - (content.clientHeight / 2);
        content.scrollTop = targetScrollTop;
    });

    updateThumb();
    setInterval(updateThumb, 1000);
}

function initAllCustomScrollbars() {
    setupCustomScrollbar('scriptArea', 'customScrollbar', 'customScrollbarThumb');
    setupCustomScrollbar('fileList', 'fileListScrollbar', 'fileListScrollbarThumb');
    setupCustomScrollbar('fileBrowser', 'fileBrowserScrollbar', 'fileBrowserScrollbarThumb');
}


