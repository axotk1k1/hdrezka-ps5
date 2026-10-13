/* HDrezka client — same unofficial endpoints as SuperZombi/HdRezkaApi,
   kristal374/hdrezka-api and n0madic/go-hdrezka. All site traffic goes
   through the local /proxy so the PS5 browser is not blocked by CORS. */
(function (global) {
  "use strict";

  var KNOWN_SALTS = [
    "IyMjI14hISMjIUBA",
    "QEBAQEAhIyMhXl5e",
    "JCQhIUAkJEBeIUAjJCRA",
    "JCQjISFAIyFAIyM=",
    "Xl5eIUAjIyEhIyM=",
  ];
  var TRASH_CHARS = ["@", "#", "!", "^", "$"];
  var QUALITY_ORDER = ["2160p", "1080p Ultra", "1080p", "720p", "480p", "360p"];

  var state = {
    origin: "",
  };

  function absUrl(url) {
    if (!url) return "";
    if (/^https?:\/\//i.test(url)) return url;
    var origin = state.origin || "";
    if (url.charAt(0) === "/") return origin + url;
    return origin + "/" + url;
  }

  function imgSrc(url) {
    url = absUrl(url);
    if (!url) return "";
    return "/img?url=" + encodeURIComponent(url);
  }

  function streamSrc(url) {
    url = absUrl(url);
    if (!url) return "";
    return "/stream?url=" + encodeURIComponent(url);
  }

  function encodeForm(obj) {
    var parts = [];
    Object.keys(obj).forEach(function (key) {
      if (obj[key] === undefined || obj[key] === null) return;
      parts.push(encodeURIComponent(key) + "=" + encodeURIComponent(String(obj[key])));
    });
    return parts.join("&");
  }

  function product(list, repeat) {
    var out = [[]];
    for (var n = 0; n < repeat; n += 1) {
      var next = [];
      for (var i = 0; i < out.length; i += 1) {
        for (var j = 0; j < list.length; j += 1) {
          next.push(out[i].concat(list[j]));
        }
      }
      out = next;
    }
    return out;
  }

  function b64encodeUtf8(str) {
    return btoa(str);
  }

  function b64decode(str) {
    var pad = str.length % 4;
    if (pad) str += "====".slice(pad);
    str = str.replace(/-/g, "+").replace(/_/g, "/");
    try {
      return decodeURIComponent(
        Array.prototype.map
          .call(atob(str), function (c) {
            return "%" + ("00" + c.charCodeAt(0).toString(16)).slice(-2);
          })
          .join("")
      );
    } catch (err) {
      try {
        return atob(str);
      } catch (err2) {
        return str;
      }
    }
  }

  function clearTrashSuperZombi(data) {
    var trashCodes = [];
    for (var len = 2; len <= 3; len += 1) {
      product(TRASH_CHARS, len).forEach(function (chars) {
        trashCodes.push(b64encodeUtf8(chars.join("")));
      });
    }
    var arr = String(data).replace("#h", "").split("//_//");
    var trashString = arr.join("");
    trashCodes.forEach(function (code) {
      trashString = trashString.split(code).join("");
    });
    return b64decode(trashString);
  }

  function clearTrash(data) {
    if (!data) return "";
    var raw = String(data);
    if (raw.charAt(0) === "[" || raw.indexOf("http") === 0) return raw;
    var s = raw;
    if (s.indexOf("#h") === 0) s = s.slice(2);
    var guard = 0;
    while (s.indexOf("//_//") >= 0 && guard < 80) {
      guard += 1;
      var idx = s.indexOf("//_//");
      var after = s.slice(idx + 5);
      var matched = false;
      for (var i = 0; i < KNOWN_SALTS.length; i += 1) {
        if (after.indexOf(KNOWN_SALTS[i]) === 0) {
          s = s.slice(0, idx) + after.slice(KNOWN_SALTS[i].length);
          matched = true;
          break;
        }
      }
      if (!matched) s = s.slice(0, idx) + after.slice(16);
    }
    var decoded = b64decode(s);
    if (decoded && (decoded.charAt(0) === "[" || decoded.indexOf("http") >= 0)) {
      return decoded;
    }
    try {
      return clearTrashSuperZombi(raw);
    } catch (err) {
      return decoded || raw;
    }
  }

  function stripTags(html) {
    return String(html || "")
      .replace(/<[^>]*>/g, " ")
      .replace(/&nbsp;/g, " ")
      .replace(/\s+/g, " ")
      .trim();
  }

  function parseQualityLabel(raw) {
    var premium = /prem-quality|prem-icon/.test(raw);
    var label = stripTags(raw);
    return { label: label || raw.trim(), premium: premium };
  }

  function isPlayableUrl(url) {
    return /^https?:\/\//i.test(url || "");
  }

  function isTeaserUrl(url) {
    return /\/zrkms\.mp4(?:[:?#]|$)/i.test(url || "");
  }

  function parseStreamFormats(str) {
    var formats = {};
    var re = /\[([^\]]+)\]/g;
    var matches = [];
    var m;
    while ((m = re.exec(str))) {
      matches.push({ raw: m[1], start: m.index, end: m.index + m[0].length });
    }
    for (var i = 0; i < matches.length; i += 1) {
      var info = parseQualityLabel(matches[i].raw);
      var from = matches[i].end;
      var to = i + 1 < matches.length ? matches[i + 1].start : str.length;
      var urlStr = str.slice(from, to).replace(/,\s*$/, "").trim();
      if (!urlStr || /^null$/i.test(urlStr)) continue;
      var urls = urlStr.split(/\s+or\s+/).map(function (u) {
        return u.trim();
      }).filter(isPlayableUrl);
      var hls = "";
      var mp4 = "";
      var hasHlsSuffix = urls.some(function (u) {
        return /:hls:manifest\.m3u8$/.test(u);
      });
      if (hasHlsSuffix) {
        urls.forEach(function (u) {
          if (/:hls:manifest\.m3u8$/.test(u)) {
            if (!hls) hls = u;
          } else if (!mp4) mp4 = u;
        });
        if (hls && !mp4) mp4 = hls.replace(/:hls:manifest\.m3u8$/, "");
      } else {
        hls = urls[0] || "";
        mp4 = urls.length > 1 ? urls[urls.length - 1] : hls;
      }
      if (!isPlayableUrl(mp4)) mp4 = "";
      if (!isPlayableUrl(hls)) hls = "";
      if (mp4 && isTeaserUrl(mp4) && hls && !isTeaserUrl(hls)) mp4 = "";
      if (hls && isTeaserUrl(hls) && mp4 && !isTeaserUrl(mp4)) hls = "";
      var play = "";
      if (mp4 && !isTeaserUrl(mp4)) play = mp4;
      else if (hls && !isTeaserUrl(hls)) play = hls;
      else play = mp4 || hls;
      if (!isPlayableUrl(play)) continue;
      formats[info.label] = {
        hls: hls,
        mp4: mp4,
        url: play,
        isPremium: info.premium,
        isTeaser: isTeaserUrl(play),
      };
    }
    return formats;
  }

  function parseSubtitles(data, codes) {
    var out = [];
    if (!data) return out;
    String(data)
      .split(",")
      .forEach(function (entry) {
        var end = entry.indexOf("]");
        if (end < 0 || entry.charAt(0) !== "[") return;
        var title = entry.slice(1, end).trim();
        var link = entry.slice(end + 1).trim();
        if (link.indexOf(" or ") >= 0) {
          var bits = link.split(/\s+or\s+/);
          link = bits[bits.length - 1].trim();
        }
        var code = (codes && (codes[title] || codes[title.toLowerCase()])) || title;
        out.push({ code: code, title: title, url: link });
      });
    return out;
  }

  function qualityRank(label) {
    var idx = QUALITY_ORDER.indexOf(label);
    if (idx >= 0) return 1000 - idx;
    var n = parseInt(label, 10);
    return isNaN(n) ? 0 : n;
  }

  function bestQuality(formats) {
    var labels = Object.keys(formats || {}).filter(function (q) {
      return isPlayableUrl(formats[q] && formats[q].url);
    });
    var movie = labels.filter(function (q) {
      return !formats[q].isPremium && !formats[q].isTeaser;
    });
    var pool = movie.length ? movie : labels.filter(function (q) {
      return !formats[q].isTeaser;
    });
    if (!pool.length) pool = labels;
    pool.sort(function (a, b) {
      return qualityRank(b) - qualityRank(a);
    });
    return pool[0] || "";
  }

  function cdnSeriesUrl() {
    return "/ajax/get_cdn_series/?t=" + Date.now();
  }

  async function loadConfig() {
    var res = await fetch("/api/config");
    var cfg = await res.json();
    state.origin = (cfg.origin || "").replace(/\/$/, "");
    return cfg;
  }

  async function saveConfig(patch) {
    var res = await fetch("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(patch),
    });
    var cfg = await res.json();
    if (cfg.origin) state.origin = cfg.origin.replace(/\/$/, "");
    return cfg;
  }

  async function siteFetch(url, opts) {
    opts = opts || {};
    if (!state.origin) await loadConfig();
    var target = absUrl(url);
    var form = null;
    if (opts.form) {
      form = {};
      Object.keys(opts.form).forEach(function (key) {
        if (opts.form[key] === undefined || opts.form[key] === null) return;
        form[key] = String(opts.form[key]);
      });
    }
    var payload = {
      url: target,
      method: opts.method || "GET",
      headers: opts.headers || {},
      form: form,
      body: opts.body || null,
    };
    var res = await fetch("/proxy", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    var ctype = res.headers.get("content-type") || "";
    var finalUrl = res.headers.get("x-final-url") || target;
    var text = await res.text();
    return { ok: res.ok, status: res.status, ctype: ctype, text: text, url: finalUrl };
  }

  function parseDoc(html) {
    return new DOMParser().parseFromString(html, "text/html");
  }

  function isChallenge(doc) {
    var title = ((doc.querySelector("title") || {}).textContent || "").trim();
    if (title === "Just a moment..." || title === "DDoS-Guard" || title === "DDOS-GUARD" || title === "Verify") {
      return title || "challenge";
    }
    if (doc.querySelector("#anubis_challenge, script[src*='.within.website']")) return "anubis";
    if (doc.querySelector('script[src*="challenges.cloudflare.com"]')) return "cloudflare";
    return "";
  }

  function categoryFromUrl(url) {
    var path = "";
    try {
      path = new URL(absUrl(url), "https://dummy.local").pathname;
    } catch (err) {
      path = url || "";
    }
    if (path.indexOf("/films/") === 0) return "films";
    if (path.indexOf("/series/") === 0) return "series";
    if (path.indexOf("/cartoons/") === 0) return "cartoons";
    if (path.indexOf("/animation/") === 0) return "animation";
    if (path.indexOf("/show/") === 0) return "show";
    return "films";
  }

  function parseCoverItem(el) {
    var a = el.querySelector(".b-content__inline_item-link a") || el.querySelector("a[href]");
    var img = el.querySelector(".b-content__inline_item-cover img") || el.querySelector("img");
    var desc = el.querySelector(".b-content__inline_item-link div");
    var info = el.querySelector("span.info");
    var href = el.getAttribute("data-url") || (a && a.getAttribute("href")) || "";
    if (!href && !a) return null;
    var coverEl = el.querySelector(".b-content__inline_item-cover");
    var isWait =
      (coverEl && /wait/i.test(coverEl.className || "")) ||
      !!(coverEl && coverEl.querySelector(".wait, i.wait"));
    return {
      id: el.getAttribute("data-id") || "",
      title: a ? (a.textContent || "").trim() : stripTags(el.textContent).slice(0, 120),
      url: absUrl(href),
      cover: img ? img.getAttribute("src") || img.getAttribute("data-src") || "" : "",
      description: desc ? stripTags(desc.innerHTML) : "",
      info: info ? stripTags(info.innerHTML) : desc ? stripTags(desc.innerHTML) : "",
      category: categoryFromUrl(href),
      isWait: isWait,
    };
  }

  function parseCoverItems(html, rootSel) {
    var doc = parseDoc(html);
    var challenge = isChallenge(doc);
    if (challenge) {
      var err = new Error("Зеркало не ответило (" + challenge + "). Повторите запрос.");
      err.code = "challenge";
      throw err;
    }
    var scope = rootSel ? doc.querySelector(rootSel) || doc : doc;
    var items = [];
    var seen = {};
    scope.querySelectorAll(".b-content__inline_item").forEach(function (el) {
      var item = parseCoverItem(el);
      if (!item || !item.url || seen[item.url]) return;
      seen[item.url] = true;
      items.push(item);
    });
    return items;
  }

  function parseCoverPage(html) {
    var doc = parseDoc(html);
    var challenge = isChallenge(doc);
    if (challenge) {
      var err = new Error("Зеркало не ответило (" + challenge + "). Повторите запрос.");
      err.code = "challenge";
      throw err;
    }
    var items = parseCoverItems(html);
    var hasNext = !!doc.querySelector(
      "span.no-page span.b-navigation__next, .b-navigation a:not(.b-navigation__prev):last-child, .b-navigation__next"
    );
    if (!hasNext) {
      var nav = doc.querySelector(".b-navigation");
      if (nav) {
        var links = nav.querySelectorAll("a");
        var last = links[links.length - 1];
        hasNext = !!(last && /след|next|>|»/i.test(last.textContent || last.getAttribute("title") || ""));
      }
    }
    return { items: items, hasNext: hasNext, count: items.length };
  }

  function parseQuickSearch(html) {
    var doc = parseDoc(html);
    var items = [];
    doc.querySelectorAll("li").forEach(function (el) {
      var a = el.querySelector("a");
      var titleEl = el.querySelector("span.enty");
      var ratingEl = el.querySelector("span.rating");
      if (!a || !a.getAttribute("href")) return;
      var href = a.getAttribute("href");
      if (href.indexOf("/films/") < 0 && href.indexOf("/series/") < 0 && href.indexOf("/cartoons/") < 0 && href.indexOf("/animation/") < 0) {
        return;
      }
      items.push({
        title: titleEl ? titleEl.textContent.trim() : stripTags(a.textContent),
        url: absUrl(href),
        rating: ratingEl ? parseFloat(ratingEl.textContent) : null,
        cover: "",
        description: "",
        info: ratingEl ? ratingEl.textContent.trim() : "",
        category: categoryFromUrl(href),
      });
    });
    return items;
  }

  function catalogPath(kind) {
    if (kind === "films") return "/films/";
    if (kind === "series") return "/series/";
    if (kind === "cartoons") return "/cartoons/";
    if (kind === "animation") return "/animation/";
    if (kind === "new") return "/new/";
    return "/films/";
  }

  async function getCovers(kind, pageOrOpts, maybeFilter) {
    var opts = {};
    if (pageOrOpts && typeof pageOrOpts === "object") opts = pageOrOpts;
    else {
      opts.page = pageOrOpts || 1;
      opts.filter = maybeFilter || "popular";
    }
    var page = opts.page || 1;
    var filter = opts.filter || "popular";
    var path = catalogPath(kind || "films");
    var url = path + "?filter=" + encodeURIComponent(filter);
    if (page > 1) url += "&page=" + page;
    var res = await siteFetch(url);
    if (!res.ok) throw new Error("Каталог недоступен (" + res.status + ")");
    var parsed = parseCoverPage(res.text);
    parsed.page = page;
    parsed.filter = filter;
    parsed.kind = kind;
    if (opts.pageInfo) return parsed;
    return parsed.items;
  }

  async function getNewest(genreId) {
    var res = await siteFetch("/engine/ajax/get_newest_slider_content.php", {
      method: "POST",
      form: { id: String(genreId) },
    });
    if (!res.ok) throw new Error("Новинки недоступны (" + res.status + ")");
    return parseCoverItems(res.text);
  }

  async function search(query, findAllOrOpts) {
    query = String(query || "").trim();
    if (!query) return typeof findAllOrOpts === "object" && findAllOrOpts && findAllOrOpts.pageInfo ? { items: [], hasNext: false } : [];
    var opts = {};
    if (typeof findAllOrOpts === "boolean") opts.full = findAllOrOpts;
    else if (findAllOrOpts && typeof findAllOrOpts === "object") opts = findAllOrOpts;
    var page = opts.page || 1;

    if (!opts.full && page === 1) {
      var quick = await siteFetch("/engine/ajax/search.php", {
        method: "POST",
        form: { q: query },
      });
      if (!quick.ok) throw new Error("Поиск недоступен (" + quick.status + ")");
      var fast = parseQuickSearch(quick.text);
      if (fast.length && !opts.preferCovers) {
        return opts.pageInfo ? { items: fast, hasNext: false, page: 1, mode: "quick" } : fast;
      }
    }

    var url = "/search/?do=search&subaction=search&q=" + encodeURIComponent(query);
    if (page > 1) url += "&page=" + page;
    var full = await siteFetch(url);
    if (!full.ok) throw new Error("Поиск недоступен (" + full.status + ")");
    var parsed = parseCoverPage(full.text);
    parsed.page = page;
    parsed.mode = "full";
    parsed.query = query;
    if (opts.pageInfo) return parsed;
    return parsed.items;
  }

  function parsePerson(html, pageUrl) {
    var doc = parseDoc(html);
    var challenge = isChallenge(doc);
    if (challenge) {
      var err = new Error("Зеркало не ответило (" + challenge + "). Повторите запрос.");
      err.code = "challenge";
      throw err;
    }
    var name =
      text(doc.querySelector(".b-post__title h1, .b-person__name, h1")) ||
      text(doc.querySelector("title")).replace(/\s*[–—|-].*$/, "");
    var nameOrig = text(doc.querySelector(".b-post__origtitle, .b-person__orig_name"));
    var photo =
      attr(doc.querySelector(".b-sidecover img, .b-person__photo img, .b-person__poster img"), "src") ||
      attr(doc.querySelector('meta[property="og:image"]'), "content");
    var photoHq = attr(doc.querySelector(".b-sidecover a"), "href") || photo;
    var careers = [];
    var allMovies = [];
    var seen = {};

    doc.querySelectorAll(".b-person__career").forEach(function (block) {
      var titleEl =
        block.querySelector(".b-person__career_header, .title, h2, .b-person__career_title") ||
        block.previousElementSibling;
      var careerTitle = titleEl ? text(titleEl) : "Фильмография";
      if (!careerTitle || careerTitle === name) careerTitle = "Фильмография";
      var movies = [];
      block.querySelectorAll(".b-content__inline_item").forEach(function (el) {
        var item = parseCoverItem(el);
        if (!item || !item.url) return;
        movies.push(item);
        if (!seen[item.url]) {
          seen[item.url] = true;
          allMovies.push(item);
        }
      });
      if (movies.length) careers.push({ title: careerTitle, movies: movies });
    });

    if (!allMovies.length) {
      allMovies = parseCoverItems(html);
    }

    var info = {};
    doc.querySelectorAll(".b-post__info tr, .b-person__info tr, .b-person__infotable tr").forEach(function (tr) {
      var label = text(tr.querySelector(".l, td:first-child")).replace(/:$/, "").toLowerCase();
      var cell = tr.querySelector("td:not(.l)") || tr.querySelectorAll("td")[1];
      if (!cell || !label) return;
      info[label] = text(cell);
    });

    var photos = [];
    doc.querySelectorAll("ul.b-person__gallery_list img, .b-person__gallery img").forEach(function (img) {
      var src = img.getAttribute("src") || img.getAttribute("data-src") || "";
      if (src) photos.push(src);
    });

    return {
      id: (pageUrl.match(/\/person\/(\d+)/) || [])[1] || "",
      url: pageUrl,
      name: name,
      nameOrig: nameOrig,
      photo: photo,
      photoHq: photoHq,
      careers: careers,
      movies: allMovies,
      photos: photos,
      dateOfBirth: info["дата рождения"] || "",
      placeOfBirth: info["место рождения"] || "",
      dateOfDeath: info["дата смерти"] || "",
      placeOfDeath: info["место смерти"] || "",
      height: info["рост"] || "",
    };
  }

  async function getPerson(url) {
    var res = await siteFetch(url);
    if (!res.ok) throw new Error("Страница персоны недоступна (" + res.status + ")");
    return parsePerson(res.text, res.url || absUrl(url));
  }

  async function getMoviesFromLink(url, page) {
    page = page || 1;
    var target = absUrl(url);
    if (page > 1) {
      if (target.indexOf("page=") >= 0) target = target.replace(/([?&])page=\d+/, "$1page=" + page);
      else target += (target.indexOf("?") >= 0 ? "&" : "?") + "page=" + page;
    }
    var res = await siteFetch(target);
    if (!res.ok) throw new Error("Список недоступен (" + res.status + ")");
    var parsed = parseCoverPage(res.text);
    parsed.page = page;
    parsed.url = target;
    return parsed;
  }

  function attr(el, name) {
    return el ? el.getAttribute(name) || "" : "";
  }

  function text(el) {
    return el ? stripTags(el.textContent) : "";
  }

  function parseVideo(html, pageUrl) {
    var doc = parseDoc(html);
    var challenge = isChallenge(doc);
    if (challenge) {
      var err = new Error("Зеркало не ответило (" + challenge + "). Повторите запрос.");
      err.code = "challenge";
      throw err;
    }
    if (text(doc.querySelector("title")) === "Sign In") {
      throw new Error("Зеркало требует вход. Укажите cookie аккаунта в Настройках.");
    }
    var id =
      attr(doc.querySelector("#post_id"), "value") ||
      attr(doc.querySelector("#user-favorites-holder, .b-userset__fav_holder"), "data-post_id") ||
      attr(doc.querySelector("#send-video-issue"), "data-id") ||
      "";
    if (!id) {
      var init = html.match(/initCDN(?:Series|Movies)Events\((\d+)/);
      if (init) id = init[1];
    }
    var ogType = attr(doc.querySelector('meta[property="og:type"]'), "content");
    var isSeries = ogType === "video.tv_series";
    var translators = [];
    var seenTranslators = {};
    var list = doc.querySelector("#translators-list");
    if (list) {
      Array.prototype.forEach.call(list.querySelectorAll("[data-translator_id]"), function (child) {
        var tid = child.getAttribute("data-translator_id");
        if (!tid || seenTranslators[tid]) return;
        seenTranslators[tid] = true;
        var name = (child.getAttribute("title") || child.textContent || "").trim();
        var img = child.querySelector("img");
        var lang = img ? img.getAttribute("title") : "";
        if (lang && name.indexOf(lang) < 0) name += " (" + lang + ")";
        translators.push({
          id: tid,
          name: name,
          premium: (child.className || "").indexOf("b-prem_translator") >= 0,
          camrip: child.getAttribute("data-camrip") || "0",
          ads: child.getAttribute("data-ads") || "0",
          director: child.getAttribute("data-director") || "0",
        });
      });
    }
    if (!translators.length) {
      var initTr = html.match(/initCDN(?:Series|Movies)Events\(\d+,\s*(\d+)/);
      var name = "Озвучка";
      var rows = doc.querySelectorAll(".b-post__info tr");
      rows.forEach(function (tr) {
        if ((tr.textContent || "").indexOf("переводе") >= 0) {
          var tds = tr.querySelectorAll("td");
          name = text(tds[tds.length - 1]) || name;
        }
      });
      if (initTr) translators.push({ id: initTr[1], name: name, premium: false, camrip: "0", ads: "0", director: "0" });
    }
    var seasons = [];
    doc.querySelectorAll(".b-simple_season__item").forEach(function (el) {
      seasons.push({ id: el.getAttribute("data-tab_id"), title: text(el) });
    });
    var episodes = {};
    doc.querySelectorAll(".b-simple_episode__item").forEach(function (el) {
      var sid = el.getAttribute("data-season_id");
      if (!episodes[sid]) episodes[sid] = [];
      episodes[sid].push({
        id: el.getAttribute("data-episode_id"),
        title: text(el),
      });
    });
    var cover = attr(doc.querySelector(".b-sidecover img"), "src");
    var coverHq = attr(doc.querySelector(".b-sidecover a"), "href") || cover;
    var backdrop =
      attr(doc.querySelector('meta[property="og:image"]'), "content") || coverHq || cover;
    var ratingEl = doc.querySelector(".b-post__rating .num");
    var votesEl = doc.querySelector(".b-post__rating .votes");
    var info = parseInfoTable(doc);
    var people = parsePeople(doc);
    return {
      id: id,
      url: pageUrl,
      name: text(doc.querySelector(".b-post__title")),
      original: text(doc.querySelector(".b-post__origtitle")),
      description: text(doc.querySelector(".b-post__description_text")),
      cover: cover,
      coverHq: coverHq,
      backdrop: backdrop,
      isSeries: isSeries,
      category: categoryFromUrl(pageUrl),
      rating: ratingEl ? parseFloat(ratingEl.textContent) : null,
      votes: votesEl ? stripTags(votesEl.textContent).replace(/[()]/g, "") : "",
      ratings: info.ratings,
      duration: info.duration,
      age: info.age,
      year: info.year,
      releaseDate: info.releaseDate,
      slogan: info.slogan,
      countries: info.countries,
      genres: info.genres,
      lists: info.lists,
      collections: info.collections,
      people: people,
      cast: people.filter(function (p) {
        return /актер|актриса/i.test(p.job || "");
      }),
      crew: people.filter(function (p) {
        return !/актер|актриса/i.test(p.job || "");
      }),
      hasTrailer: !!doc.querySelector(".show-trailer"),
      translators: translators,
      favs: attr(doc.querySelector("#ctrl_favs"), "value"),
      seasons: seasons,
      episodes: episodes,
    };
  }

  function parseInfoTable(doc) {
    var out = {
      ratings: { imdb: null, kinopoisk: null, hdrezka: null },
      duration: "",
      age: "",
      year: "",
      releaseDate: "",
      slogan: "",
      countries: [],
      genres: [],
      lists: [],
      collections: [],
    };
    var ratingEl = doc.querySelector(".b-post__rating .num");
    var votesEl = doc.querySelector(".b-post__rating .votes");
    if (ratingEl) {
      out.ratings.hdrezka = {
        value: parseFloat(ratingEl.textContent),
        votes: votesEl ? stripTags(votesEl.textContent).replace(/[()]/g, "").trim() : "",
      };
    }
    var imdb = doc.querySelector(".b-post__info_rates.imdb .bold");
    if (imdb) {
      var imdbVotes = doc.querySelector(".b-post__info_rates.imdb i");
      out.ratings.imdb = {
        value: parseFloat(imdb.textContent),
        votes: imdbVotes ? stripTags(imdbVotes.textContent).replace(/[()]/g, "").trim() : "",
      };
    }
    var kp = doc.querySelector(".b-post__info_rates.kp .bold");
    if (kp) {
      var kpVotes = doc.querySelector(".b-post__info_rates.kp i");
      out.ratings.kinopoisk = {
        value: parseFloat(kp.textContent),
        votes: kpVotes ? stripTags(kpVotes.textContent).replace(/[()]/g, "").trim() : "",
      };
    }
    doc.querySelectorAll(".b-post__info tr").forEach(function (tr) {
      var label = text(tr.querySelector(".l")).replace(/:$/, "").toLowerCase();
      var cell = tr.querySelector("td:not(.l)") || tr.querySelector("td");
      if (!cell) return;
      if (label.indexOf("слоган") >= 0) {
        out.slogan = text(cell).replace(/^«|»$/g, "").replace(/^"|"$/g, "");
      } else if (label.indexOf("дата выхода") >= 0) {
        out.releaseDate = text(cell);
        var y = text(cell).match(/(19|20)\d{2}/);
        if (y) out.year = y[0];
      } else if (label.indexOf("страна") >= 0) {
        out.countries = Array.prototype.map
          .call(cell.querySelectorAll("a"), function (a) {
            return text(a);
          })
          .filter(Boolean);
      } else if (label.indexOf("жанр") >= 0) {
        var genreNodes = cell.querySelectorAll("[itemprop='genre']");
        if (!genreNodes.length) genreNodes = cell.querySelectorAll("a");
        out.genres = Array.prototype.map
          .call(genreNodes, function (a) {
            return text(a);
          })
          .filter(Boolean);
        if (!out.genres.length) out.genres = text(cell).split(",").map(function (s) { return s.trim(); }).filter(Boolean);
      } else if (label.indexOf("возраст") >= 0) {
        var age = text(cell).match(/\d+\+/);
        out.age = age ? age[0] : text(cell).split(" ")[0];
      } else if (label.indexOf("время") >= 0 || label.indexOf("длительн") >= 0) {
        out.duration = text(cell);
      } else if (label.indexOf("входит в списки") >= 0 || label.indexOf("списки") >= 0) {
        out.lists = Array.prototype.map
          .call(cell.querySelectorAll("a"), function (a) {
            var place = "";
            var next = a.nextSibling;
            if (next && next.textContent) {
              var m = next.textContent.match(/\(([^)]+)\)/);
              if (m) place = m[1];
            }
            return { title: text(a), place: place, url: a.getAttribute("href") || "" };
          })
          .filter(function (x) {
            return x.title;
          });
      } else if (label.indexOf("из серии") >= 0) {
        out.collections = Array.prototype.map
          .call(cell.querySelectorAll("a"), function (a) {
            return { title: text(a), url: a.getAttribute("href") || "" };
          })
          .filter(function (x) {
            return x.title;
          });
      }
    });
    if (!out.year) {
      var titleYear = text(doc.querySelector(".b-post__title")).match(/(19|20)\d{2}/);
      if (titleYear) out.year = titleYear[0];
    }
    return out;
  }

  function parsePeople(doc) {
    var people = [];
    var seen = {};
    doc.querySelectorAll(".person-name-item").forEach(function (el) {
      var pid = el.getAttribute("data-id") || "";
      var name = text(el.querySelector("[itemprop='name']")) || text(el);
      if (!name) return;
      var key = pid || name;
      if (seen[key]) return;
      seen[key] = true;
      people.push({
        id: pid,
        name: name,
        job: el.getAttribute("data-job") || "",
        photo: el.getAttribute("data-photo") || "",
        url: attr(el.querySelector("a"), "href"),
      });
    });
    return people;
  }

  async function getTrailer(videoId) {
    var res = await siteFetch("/engine/ajax/gettrailervideo.php", {
      method: "POST",
      form: { id: videoId },
    });
    if (!res.ok) throw new Error("Трейлер недоступен");
    var json;
    try {
      json = JSON.parse(res.text);
    } catch (err) {
      throw new Error("Некорректный ответ трейлера");
    }
    var html = json.code || json.message || "";
    var yt = html.match(/youtube\.com\/embed\/([\w-]{6,})/i) || html.match(/youtu\.be\/([\w-]{6,})/i);
    return {
      success: !!yt || /iframe/i.test(html),
      html: html,
      youtubeId: yt ? yt[1] : "",
      thumb: yt ? "https://i.ytimg.com/vi/" + yt[1] + "/hqdefault.jpg" : "",
    };
  }

  async function getVideo(url) {
    var res = await siteFetch(url);
    if (!res.ok) throw new Error("Страница недоступна (" + res.status + ")");
    return parseVideo(res.text, res.url || absUrl(url));
  }

  async function getEpisodes(video, translatorId) {
    var res = await siteFetch(cdnSeriesUrl(), {
      method: "POST",
      form: {
        id: video.id,
        translator_id: translatorId,
        favs: video.favs || undefined,
        action: "get_episodes",
      },
    });
    if (!res.ok) throw new Error("Список серий недоступен");
    var json;
    try {
      json = JSON.parse(res.text);
    } catch (err) {
      throw new Error("Некорректный ответ списка серий");
    }
    if (!json.success) throw new Error(json.message || "Не удалось получить серии");
    var seasons = [];
    var episodes = {};
    parseDoc(json.seasons || "").querySelectorAll(".b-simple_season__item").forEach(function (el) {
      seasons.push({ id: el.getAttribute("data-tab_id"), title: text(el) });
    });
    parseDoc(json.episodes || "").querySelectorAll(".b-simple_episode__item").forEach(function (el) {
      var sid = el.getAttribute("data-season_id");
      if (!episodes[sid]) episodes[sid] = [];
      episodes[sid].push({ id: el.getAttribute("data-episode_id"), title: text(el) });
    });
    return { seasons: seasons, episodes: episodes };
  }

  async function getStream(video, opts) {
    opts = opts || {};
    var form;
    if (video.isSeries) {
      form = {
        id: video.id,
        translator_id: opts.translatorId,
        season: opts.season,
        episode: opts.episode,
        favs: video.favs || undefined,
        action: "get_stream",
      };
    } else {
      var tr = (video.translators || []).filter(function (t) {
        return String(t.id) === String(opts.translatorId);
      })[0] || {};
      form = {
        id: video.id,
        translator_id: opts.translatorId,
        is_camrip: tr.camrip || "0",
        is_ads: tr.ads || "0",
        is_director: tr.director || "0",
        favs: video.favs || undefined,
        action: "get_movie",
      };
    }
    var res = await siteFetch(cdnSeriesUrl(), { method: "POST", form: form });
    if (!res.ok) throw new Error("Стрим недоступен (" + res.status + ")");
    var json;
    try {
      json = JSON.parse(res.text);
    } catch (err) {
      throw new Error("Некорректный ответ плеера");
    }
    if (!json.success || !json.url) throw new Error(json.message || "Нет ссылки на видео");
    var decoded = clearTrash(json.url);
    var formats = parseStreamFormats(decoded);
    var subtitles = parseSubtitles(json.subtitle, json.subtitle_lns || {});
    var best = bestQuality(formats);
    return {
      formats: formats,
      qualities: Object.keys(formats).sort(function (a, b) {
        return qualityRank(b) - qualityRank(a);
      }),
      best: best,
      previewOnly: !!(best && formats[best] && formats[best].isTeaser),
      subtitles: subtitles,
      thumbnails: json.thumbnails || "",
      raw: decoded,
    };
  }

  var posterCache = {};
  var posterPending = {};

  async function getPoster(url) {
    url = absUrl(url);
    if (!url) return null;
    if (posterCache[url]) return posterCache[url];
    if (posterPending[url]) return posterPending[url];
    posterPending[url] = siteFetch(url)
      .then(function (res) {
        if (!res.ok) return null;
        var doc = parseDoc(res.text);
        var poster = attr(doc.querySelector(".b-sidecover img"), "src");
        var full = attr(doc.querySelector(".b-sidecover a"), "href") || "";
        if (full && !/\.(jpe?g|png|webp)(\?|$)/i.test(full)) full = "";
        var art = { poster: poster || "", full: full || poster || "" };
        if (art.poster || art.full) posterCache[url] = art;
        return art.poster || art.full ? art : null;
      })
      .catch(function () {
        return null;
      })
      .finally(function () {
        delete posterPending[url];
      });
    return posterPending[url];
  }

  function parseSeriesUpdates(html) {
    var doc = parseDoc(html);
    var groups = [];
    doc.querySelectorAll(".b-seriesupdate__block").forEach(function (block) {
      var items = [];
      block.querySelectorAll(".b-seriesupdate__block_list_item").forEach(function (li) {
        var a = li.querySelector("a.b-seriesupdate__block_list_link, a[href]");
        if (!a) return;
        var href = a.getAttribute("href") || "";
        if (!href || href.indexOf("javascript:") === 0) return;
        items.push({ title: text(a), url: absUrl(href) });
      });
      if (!items.length) return;
      groups.push({ date: text(block.querySelector(".b-seriesupdate__block_date")), items: items });
    });
    return groups;
  }

  function premiumFromDoc(doc) {
    var premEl = doc.querySelector("li.b-tophead-premuser, .b-tophead-premuser");
    var premiumText = premEl ? text(premEl) : "";
    var bold = premEl ? text(premEl.querySelector("b")) : "";
    var days = null;
    var match = String(bold || premiumText).match(/(\d+)/);
    if (match) {
      var n = parseInt(match[1], 10);
      if (n <= 4000) days = n;
    }
    return { premium: !!premEl, premiumText: premiumText, days: days };
  }

  async function getAccount() {
    var home = await siteFetch("/");
    if (!home.ok) throw new Error("Профиль недоступен (" + home.status + ")");
    var doc = parseDoc(home.text);
    var challenge = isChallenge(doc);
    if (challenge) throw new Error("Зеркало не ответило (" + challenge + "). Повторите запрос.");
    var id = attr(doc.querySelector("#member_user_id"), "value") || "0";
    var prem = premiumFromDoc(doc);
    var account = {
      loggedIn: id !== "0" && id !== "",
      id: id,
      name: "",
      email: "",
      avatar: "",
      premium: prem.premium,
      premiumText: prem.premiumText,
      days: prem.days,
      updates: parseSeriesUpdates(home.text),
    };
    if (!account.loggedIn) return account;
    var page = await siteFetch("/user/" + encodeURIComponent(id) + "/");
    if (!page.ok) return account;
    var user = parseDoc(page.text);
    if (isChallenge(user)) return account;
    var still = attr(user.querySelector("#member_user_id"), "value") || "0";
    if (still === "0") return account;
    var title = text(user.querySelector("title"));
    if (title && title.indexOf("Смотреть фильмы") !== 0) {
      account.name = title.split(/[|—–]/)[0].replace(/\s+HDrezka.*$/i, "").trim();
    }
    var heading = text(user.querySelector(".b-userprofile__name, .b-userprofile h1, .b-content__htitle h1"));
    if (heading && heading !== "Мои закладки" && heading !== "Досмотреть") account.name = heading || account.name;
    var emailInput = user.querySelector("input#email, input[name='email'], input[type='email']");
    account.email = attr(emailInput, "value") || account.email;
    var avatar = attr(user.querySelector(".b-userprofile__avatar_holder img"), "src");
    if (avatar && /upload|avatar|user|static/i.test(avatar)) account.avatar = absUrl(avatar);
    var premUser = premiumFromDoc(user);
    if (premUser.premium) {
      account.premium = true;
      account.premiumText = premUser.premiumText || account.premiumText;
      if (premUser.days != null) account.days = premUser.days;
    }
    var more = parseSeriesUpdates(page.text);
    if (more.length) account.updates = more;
    return account;
  }

  async function logout() {
    var res = await siteFetch("/logout");
    if (!res.ok) throw new Error("Не удалось выйти (" + res.status + ")");
    return true;
  }

  async function login(email, password) {
    var res = await siteFetch("/ajax/login/", {
      method: "POST",
      form: { login_name: email, login_password: password, login_not_save: "0" },
    });
    var json;
    try {
      json = JSON.parse(res.text);
    } catch (err) {
      throw new Error("Некорректный ответ входа");
    }
    if (!json.success) throw new Error(json.message || "Вход не выполнен");
    return true;
  }

  global.HdRezka = {
    loadConfig: loadConfig,
    saveConfig: saveConfig,
    imgSrc: imgSrc,
    streamSrc: streamSrc,
    absUrl: absUrl,
    clearTrash: clearTrash,
    parseStreamFormats: parseStreamFormats,
    getCovers: getCovers,
    getNewest: getNewest,
    search: search,
    getVideo: getVideo,
    getPoster: getPoster,
    getPerson: getPerson,
    getMoviesFromLink: getMoviesFromLink,
    getEpisodes: getEpisodes,
    getStream: getStream,
    getTrailer: getTrailer,
    login: login,
    logout: logout,
    getAccount: getAccount,
    catalogPath: catalogPath,
    bestQuality: bestQuality,
    QUALITY_ORDER: QUALITY_ORDER,
    get origin() {
      return state.origin;
    },
  };
})(window);
