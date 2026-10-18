// 由 tools/aidogui/html_to_aid.mjs 注入草稿 HTML 里跑（无头 Edge + --dump-dom 取回）。
// 只干一件事：把每个 [data-name] 元素的几何与计算样式拍成 JSON，塞进 <script id="embark-probe">。
// 之所以单独成文件而不是内联字符串：这段代码要在浏览器里跑，写在 .mjs 里会被转义/模板字符串搅坏。
(function () {
  "use strict";

  var ISLAND_ID = "embark-probe";

  function num(value) {
    var n = parseFloat(value);
    return Number.isFinite(n) ? n : 0;
  }

  function collect() {
    var named = Array.prototype.slice.call(document.querySelectorAll("[data-name]"));
    var rows = [];
    named.forEach(function (el) {
      var rect = el.getBoundingClientRect();
      var cs = window.getComputedStyle(el);
      var parentName = null;
      var origin = [0, 0];
      for (var p = el.parentElement; p; p = p.parentElement) {
        if (p.hasAttribute && p.hasAttribute("data-name")) {
          parentName = p.getAttribute("data-name");
          break;
        }
      }
      if (parentName !== null) {
        var holder = null;
        for (var i = 0; i < named.length; i += 1) {
          if (named[i].getAttribute("data-name") === parentName) {
            holder = named[i];
            break;
          }
        }
        if (holder) {
          var pr = holder.getBoundingClientRect();
          var pcs = window.getComputedStyle(holder);
          // LVGL 的子控件坐标以父控件的内容区为原点（边框 + 内边距之后），这里跟着对齐。
          origin = [
            pr.left + num(pcs.borderLeftWidth) + num(pcs.paddingLeft),
            pr.top + num(pcs.borderTopWidth) + num(pcs.paddingTop)
          ];
        }
      }
      var own = "";
      Array.prototype.forEach.call(el.childNodes, function (node) {
        if (node.nodeType === 3) own += node.textContent;
      });
      rows.push({
        name: el.getAttribute("data-name"),
        tag: el.tagName.toLowerCase(),
        type: el.getAttribute("data-type"),
        action: el.getAttribute("data-action"),
        pressedBg: el.getAttribute("data-pressed-bg"),
        font: el.getAttribute("data-font"),
        scroll: el.getAttribute("data-scroll"),
        clickable: el.hasAttribute("data-clickable") || el.tagName === "BUTTON" || el.tagName === "A",
        textMark: el.hasAttribute("data-text"),
        parentName: parentName,
        rect: [rect.left, rect.top, rect.width, rect.height],
        origin: origin,
        style: {
          color: cs.color,
          backgroundColor: cs.backgroundColor,
          fontSize: cs.fontSize,
          fontFamily: cs.fontFamily,
          textAlign: cs.textAlign,
          borderRadius: cs.borderTopLeftRadius,
          borderTopWidth: cs.borderTopWidth,
          padding: [cs.paddingLeft, cs.paddingTop, cs.paddingRight, cs.paddingBottom]
        },
        text: own.replace(/\s+/g, " ").trim()
      });
    });
    return rows;
  }

  function emit() {
    var rows;
    try {
      rows = collect();
    } catch (err) {
      rows = { error: String((err && err.stack) || err) };
    }
    var island = document.createElement("script");
    island.id = ISLAND_ID;
    island.type = "application/json";
    island.textContent = JSON.stringify(rows);
    document.body.appendChild(island);
  }

  function ready() {
    if (document.body) emit();
    else document.addEventListener("DOMContentLoaded", emit, { once: true });
  }

  // 字体加载完再量：草稿里没写 font-family 也不该量成回退字体的宽度。
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(ready, ready);
  else ready();
})();
