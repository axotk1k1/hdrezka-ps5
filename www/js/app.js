(function () {
  "use strict";

  var views = ["discover", "catalog", "genres", "mylist", "continue", "bookmarks", "search", "history", "details", "person", "player", "settings"];
  var tabs = ["discover", "catalog", "genres", "mylist", "continue", "bookmarks", "search", "settings"];
  var catalogKind = "films";
  var catalogFilter = "popular";
  var catalogPage = 1;
  var catalogHasNext = false;
  var searchQuery = "";
  var searchPage = 1;
  var searchFull = false;
  var searchHasNext = false;
  var personReturnView = "details";
  var homeKey = "films:last";
  var heroSlides = [];
  var heroIndex = 0;
  var heroTimer = null;
  var profileAccount = null;
  var playerChromeTimer = null;
  var playerScrubbing = false;
  var playerJustRevealed = false;
  var seekFlashTimer = null;
  var GENRES = [
    { title: "Боевики", url: "/films/boeviki/" },
    { title: "Комедии", url: "/films/komedii/" },
    { title: "Драмы", url: "/films/drama/" },
    { title: "Ужасы", url: "/films/uzhasy/" },
    { title: "Фантастика", url: "/films/fantastika/" },
    { title: "Фэнтези", url: "/films/fentezi/" },
    { title: "Триллеры", url: "/films/trillery/" },
    { title: "Детективы", url: "/films/detektivy/" },
    { title: "Мелодрамы", url: "/films/melodramy/" },
    { title: "Приключения", url: "/films/priklyucheniya/" },
    { title: "Сериалы", url: "/series/" },
    { title: "Мультфильмы", url: "/cartoons/" },
    { title: "Аниме", url: "/animation/" },
  ];
  var appState = {
    view: "discover",
    details: null,
    person: null,
    stream: null,
    selectedTranslator: null,
    selectedSeason: null,
    selectedEpisode: null,
    selectedQuality: null,
    selectedSub: null,
    lastFocus: {},
  };
  var LS_HISTORY = "rezka.history";
  var LS_CONTINUE = "rezka.continue";
  var LS_FAVORITES = "rezka.favorites";
  var MONTHS_RU = [
    "января", "февраля", "марта", "апреля", "мая", "июня",
    "июля", "августа", "сентября", "октября", "ноября", "декабря",
  ];
  var modalState = { kind: null, onClose: null };

  function $(sel, root) {
    return (root || document).querySelector(sel);
  }
  function $$(sel, root) {
    return Array.prototype.slice.call((root || document).querySelectorAll(sel));
  }

  function toast(msg) {
    var el = $("#toast");
    el.textContent = msg;
    el.classList.add("is-on");
    clearTimeout(toast._t);
    toast._t = setTimeout(function () {
      el.classList.remove("is-on");
    }, 3200);
  }

  function showView(name, opts) {
    opts = opts || {};
    appState.view = name;
    closeModal(true);
    views.forEach(function (id) {
      var node = $("#view-" + id);
      if (node) node.hidden = id !== name;
    });
    $$(".tab").forEach(function (tab) {
      var view = tab.getAttribute("data-view");
      var kind = tab.getAttribute("data-kind");
      var active = false;
      if (name === "catalog" && view === "catalog") {
        active = kind === catalogKind;
      } else {
        active = view === name && !kind;
      }
      tab.classList.toggle("is-active", active);
    });
    var immersive = name === "details" || name === "player";
    $("#topbar").classList.toggle("is-immersive", immersive);
    document.body.classList.toggle("is-immersive", immersive);
    if (!opts.keepFocus) {
      setTimeout(function () {
        TvNav.first($("#view-" + name));
      }, 30);
    }
  }

  function readJson(key, fallback) {
    try {
      return JSON.parse(localStorage.getItem(key) || "") || fallback;
    } catch (err) {
      return fallback;
    }
  }
  function writeJson(key, value) {
    localStorage.setItem(key, JSON.stringify(value));
  }

  function historyList() {
    return readJson(LS_HISTORY, []);
  }
  function rememberHistory(item) {
    var list = historyList().filter(function (x) {
      return x.url !== item.url;
    });
    list.unshift({
      title: item.title || item.name,
      url: item.url,
      cover: item.cover,
      category: item.category,
      at: Date.now(),
    });
    writeJson(LS_HISTORY, list.slice(0, 60));
  }

  function continueMap() {
    return readJson(LS_CONTINUE, {});
  }
  function saveContinue(key, data) {
    var map = continueMap();
    map[key] = data;
    writeJson(LS_CONTINUE, map);
  }
  function favoritesMap() {
    return readJson(LS_FAVORITES, {});
  }
  function isFavorite(url) {
    return !!favoritesMap()[url];
  }
  function toggleFavorite(video) {
    var map = favoritesMap();
    if (map[video.url]) {
      delete map[video.url];
      writeJson(LS_FAVORITES, map);
      return false;
    }
    map[video.url] = {
      title: video.name,
      url: video.url,
      cover: video.cover,
      at: Date.now(),
    };
    writeJson(LS_FAVORITES, map);
    return true;
  }

  function formatClock() {
    var now = new Date();
    var hh = String(now.getHours()).padStart(2, "0");
    var mm = String(now.getMinutes()).padStart(2, "0");
    $("#clock-time").textContent = hh + ":" + mm;
    if ($("#clock-date")) {
      $("#clock-date").textContent = now.getDate() + " " + MONTHS_RU[now.getMonth()];
    }
  }

  function formatTime(sec) {
    sec = Math.max(0, Math.floor(sec || 0));
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = sec % 60;
    if (h > 0) return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0");
    return m + ":" + String(s).padStart(2, "0");
  }

  function formatVotes(raw) {
    if (!raw) return "";
    var n = String(raw).replace(/\s+/g, "").replace(/[()]/g, "");
    var num = parseInt(n, 10);
    if (!num) return raw;
    if (num >= 1000000) return (num / 1000000).toFixed(1).replace(/\.0$/, "") + "M";
    if (num >= 1000) return (num / 1000).toFixed(1).replace(/\.0$/, "") + "K";
    return String(num);
  }

  function translatorById(id) {
    var v = appState.details;
    if (!v) return null;
    return (v.translators || []).filter(function (t) {
      return String(t.id) === String(id);
    })[0] || null;
  }

  function closeModal(silent) {
    var root = $("#modal-root");
    if (root.hidden) return;
    root.hidden = true;
    $("#modal-panel").innerHTML = "";
    var prev = modalState;
    modalState = { kind: null, onClose: null };
    if (!silent && prev.onClose) prev.onClose();
    if (!silent && appState.view === "details") {
      setTimeout(function () {
        TvNav.first($("#view-details"));
      }, 20);
    }
  }

  function openModal(kind, html, focusSel) {
    modalState.kind = kind;
    $("#modal-panel").innerHTML = html;
    $("#modal-root").hidden = false;
    setTimeout(function () {
      var focus = focusSel ? $(focusSel, $("#modal-panel")) : null;
      if (focus) TvNav.focus(focus, true);
      else TvNav.first($("#modal-panel"));
    }, 20);
  }

  function modalRoot() {
    return $("#modal-root").hidden ? null : $("#modal-panel");
  }

  function cardHtml(item) {
    var cover = item.cover ? HdRezka.imgSrc(item.cover) : "";
    var meta = item.info || item.description || "";
    return (
      '<button class="card' +
      (item.isWait ? " is-wait" : "") +
      '" data-nav data-url="' +
      escapeAttr(item.url) +
      '" type="button">' +
      '<div class="card-poster">' +
      (cover ? '<img src="' + escapeAttr(cover) + '" alt="">' : '<div class="card-fallback"></div>') +
      "</div>" +
      '<div class="card-meta">' +
      "<h3>" +
      escapeHtml(item.title || item.name || "") +
      "</h3>" +
      (meta ? "<p>" + escapeHtml(meta) + "</p>" : "") +
      "</div></button>"
    );
  }

  function escapeHtml(s) {
    return String(s || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }
  function escapeAttr(s) {
    return escapeHtml(s);
  }

  function bindCards(root) {
    $$(".card", root).forEach(function (btn) {
      btn.addEventListener("click", function () {
        openDetails(btn.getAttribute("data-url"));
      });
    });
  }

  function renderRow(el, items, opts) {
    opts = opts || {};
    if (!items || !items.length) {
      if (!opts.append) el.innerHTML = '<div class="empty">Ничего не найдено</div>';
      return;
    }
    var html = items.map(cardHtml).join("");
    if (opts.append) {
      var empty = $(".empty", el);
      if (empty) empty.remove();
      el.insertAdjacentHTML("beforeend", html);
      bindCards(el);
      return;
    }
    el.innerHTML = html;
    bindCards(el);
  }

  function setBusy(on) {
    $("#boot").hidden = !on;
  }

  function homeCardHtml(item, rank) {
    var cover = item.cover ? HdRezka.imgSrc(item.cover) : "";
    return (
      '<button class="home-card" data-nav data-url="' +
      escapeAttr(item.url) +
      '" type="button">' +
      (rank
        ? '<span class="home-badge top">TOP ' + rank + "</span>"
        : "") +
      '<span class="home-badge q">HD</span>' +
      '<div class="home-card-poster">' +
      (cover
        ? '<img src="' + escapeAttr(cover) + '" alt="">'
        : '<div class="home-card-fallback"></div>') +
      '<div class="home-card-meta"><h3>' +
      escapeHtml(item.title || item.name || "") +
      "</h3></div></div></button>"
    );
  }

  function renderHomeRow(el, items) {
    if (!el) return;
    if (!items || !items.length) {
      el.innerHTML = '<div class="empty">Ничего не найдено</div>';
      return;
    }
    el.innerHTML = items
      .slice(0, 4)
      .map(function (item, i) {
        return homeCardHtml(item, i === 0 ? 1 : 0);
      })
      .join("");
    $$(".home-card", el).forEach(function (btn) {
      btn.addEventListener("click", function () {
        openDetails(btn.getAttribute("data-url"));
      });
      btn.addEventListener("focus", function () {
        $$(".home-card", el).forEach(function (c) {
          c.classList.toggle("is-active", c === btn);
        });
        var er = btn.getBoundingClientRect();
        var pr = el.getBoundingClientRect();
        if (er.left < pr.left + 20) el.scrollBy({ left: er.left - pr.left - 20, behavior: "smooth" });
        else if (er.right > pr.right - 20) el.scrollBy({ left: er.right - pr.right + 20, behavior: "smooth" });
      });
    });
    upgradeHomePosters(el);
  }

  function upgradeHomePosters(el) {
    if (!el || !HdRezka.getPoster) return;
    $$(".home-card", el).forEach(function (btn) {
      var url = btn.getAttribute("data-url");
      if (!url) return;
      HdRezka.getPoster(url).then(function (art) {
        if (!art || !art.poster) return;
        var img = btn.querySelector("img");
        if (img) img.src = HdRezka.imgSrc(art.poster);
      });
    });
  }

  function paintHero(item) {
    if (!item) return;
    $("#hero-title").textContent = item.title || item.name || "";
    $("#hero-kicker").textContent = item.category || "Новинка";
    $("#hero-meta").innerHTML =
      (item.info || item.description || "")
        .split(",")
        .slice(0, 4)
        .map(function (part, idx) {
          var t = part.trim();
          if (!t) return "";
          if (idx === 0) return "<span>" + escapeHtml(t) + "</span>";
          return '<span class="badge">' + escapeHtml(t) + "</span>";
        })
        .join(" ");
    $("#hero-desc").textContent =
      item.description || item.info || "Смотрите на неофициальном клиенте HDrezka.";
    $("#hero-play").setAttribute("data-url", item.url || "");
    $("#hero-fav").setAttribute("data-url", item.url || "");
    var bg = $("#hero-bg");
    var paintUrl = item.url || "";
    if (bg) bg.dataset.paint = paintUrl;
    if (bg && item.cover) {
      bg.style.backgroundImage = "url('" + HdRezka.imgSrc(item.cover) + "')";
    }
    if (bg && paintUrl && HdRezka.getPoster) {
      HdRezka.getPoster(paintUrl).then(function (art) {
        if (!art || bg.dataset.paint !== paintUrl) return;
        var src = art.full || art.poster;
        if (src) bg.style.backgroundImage = "url('" + HdRezka.imgSrc(src) + "')";
      });
    }
    $$("#hero-dots .home-dot").forEach(function (dot, i) {
      dot.classList.toggle("is-active", i === heroIndex);
    });
    var on = item.url && isFavorite(item.url);
    var favLabel = $("#hero-fav .btn-label");
    if (favLabel) favLabel.textContent = on ? "В списке" : "В мой список";
  }

  function setHeroSlides(items) {
    heroSlides = (items || []).filter(function (x) {
      return x && x.url;
    }).slice(0, 5);
    heroIndex = 0;
    var dots = $("#hero-dots");
    dots.innerHTML = heroSlides
      .map(function (_, i) {
        return '<button class="home-dot' + (i === 0 ? " is-active" : "") + '" data-nav data-hero="' + i + '" type="button" aria-label="Слайд ' + (i + 1) + '"></button>';
      })
      .join("");
    $$("#hero-dots [data-hero]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        heroIndex = parseInt(btn.getAttribute("data-hero"), 10) || 0;
        paintHero(heroSlides[heroIndex]);
        restartHeroTimer();
      });
    });
    paintHero(heroSlides[0]);
    restartHeroTimer();
  }

  function restartHeroTimer() {
    clearInterval(heroTimer);
    if (heroSlides.length < 2) return;
    heroTimer = setInterval(function () {
      if (appState.view !== "discover") return;
      heroIndex = (heroIndex + 1) % heroSlides.length;
      paintHero(heroSlides[heroIndex]);
    }, 7000);
  }

  function homeTitleForKey(key) {
    var map = {
      "films:last": "Последние поступления",
      "mix:last": "Последние поступления",
      "series:last": "Последние поступления · Сериалы",
      "cartoons:last": "Последние поступления · Мультфильмы",
      "animation:last": "Последние поступления · Аниме",
      "new:last": "Новинки",
      "films:popular": "Популярные фильмы",
      "films:watching": "Сейчас смотрят",
      "films:year": "За год",
      "films:added": "По дате добавления",
      "films:soon": "Скоро на сайте",
      "series:popular": "Популярные сериалы",
      "series:watching": "Сериалы · смотрят",
      "cartoons:popular": "Популярные мультфильмы",
      "cartoons:watching": "Мультфильмы · смотрят",
      "animation:popular": "Популярное аниме",
      "link:/films/boeviki/": "Боевики",
      "link:/films/komedii/": "Комедии",
      "link:/films/drama/": "Драмы",
      "link:/films/uzhasy/": "Ужасы",
      "link:/films/fantastika/": "Фантастика",
      "link:/films/fentezi/": "Фэнтези",
      "link:/films/trillery/": "Триллеры",
      "link:/films/detektivy/": "Детективы",
      "link:/films/melodramy/": "Мелодрамы",
      "link:/films/priklyucheniya/": "Приключения",
    };
    return map[key] || "Каталог";
  }

  async function loadHomeRow(key) {
    homeKey = key || homeKey;
    $$("#home-filters [data-home]").forEach(function (btn) {
      btn.classList.toggle("is-active", btn.getAttribute("data-home") === homeKey);
    });
    var rows = {
      films: $("#home-films"),
      series: $("#home-series"),
      cartoons: $("#home-cartoons"),
    };
    Object.keys(rows).forEach(function (id) {
      if (rows[id]) rows[id].innerHTML = '<div class="empty">Загрузка…</div>';
    });
    var filter = "last";
    if (homeKey.indexOf("link:") !== 0 && homeKey.indexOf(":") > 0) {
      filter = homeKey.split(":")[1] || "last";
    }
    if (filter === "soon") filter = "last";
    try {
      var packs = await Promise.all([
        homeKey.indexOf("link:") === 0
          ? HdRezka.getMoviesFromLink(homeKey.slice(5), 1).then(function (page) { return page.items || []; })
          : HdRezka.getCovers("films", { page: 1, filter: filter }),
        HdRezka.getCovers("series", { page: 1, filter: filter }),
        HdRezka.getCovers("cartoons", { page: 1, filter: filter }),
      ]);
      renderHomeRow(rows.films, packs[0] || []);
      renderHomeRow(rows.series, packs[1] || []);
      renderHomeRow(rows.cartoons, packs[2] || []);
    } catch (err) {
      Object.keys(rows).forEach(function (id) {
        if (rows[id]) rows[id].innerHTML = '<div class="empty">' + escapeHtml(err.message || "Ошибка") + "</div>";
      });
      toast(err.message);
    }
  }

  async function loadDiscover() {
    setBusy(true);
    try {
      var films = await HdRezka.getNewest(1);
      var series = await HdRezka.getNewest(2);
      var covers = await HdRezka.getCovers("films", { page: 1, filter: "last", pageInfo: true });
      var slides = (films || []).slice(0, 3).concat((series || []).slice(0, 2));
      if (!slides.length && covers && covers.items) slides = covers.items.slice(0, 5);
      setHeroSlides(slides);
      await loadHomeRow(homeKey);
    } catch (err) {
      toast(err.message || "Не удалось загрузить главную");
      try {
        await loadHomeRow(homeKey);
      } catch (e2) {}
    } finally {
      setBusy(false);
      if (appState.view === "discover") TvNav.focus($("#hero-play"), true);
    }
  }

  function renderMyList() {
    var map = favoritesMap();
    var items = Object.keys(map)
      .map(function (k) {
        return map[k];
      })
      .sort(function (a, b) {
        return (b.at || 0) - (a.at || 0);
      });
    var grid = $("#mylist-grid");
    if (!items.length) {
      grid.innerHTML = '<div class="empty">Список пуст. Добавляйте тайтлы кнопкой «В мой список».</div>';
      return;
    }
    renderRow(
      grid,
      items.map(function (x) {
        return { title: x.title, url: x.url, cover: x.cover, info: "" };
      })
    );
  }

  async function loadSiteList(kind) {
    var grid = kind === "bookmarks" ? $("#bookmarks-grid") : $("#continue-grid");
    if (!grid) return;
    grid.innerHTML = '<div class="empty">Загрузка…</div>';
    try {
      var page = await HdRezka.getMoviesFromLink(kind === "bookmarks" ? "/favorites/" : "/continue/");
      if (!page.items || !page.items.length) {
        grid.innerHTML =
          '<div class="empty">' +
          (kind === "bookmarks"
            ? "Нет закладок"
            : "Начните смотреть фильм или сериал, и он появится здесь.") +
          "</div>";
        return;
      }
      renderRow(grid, page.items);
    } catch (err) {
      grid.innerHTML = '<div class="empty">' + escapeHtml(err.message || "Ошибка") + "</div>";
      toast(err.message);
    }
  }

  async function loadGenres(url) {
    var chips = $("#genres-chips");
    if (!chips.dataset.ready) {
      chips.innerHTML = GENRES.map(function (g, i) {
        return (
          '<button class="chip' +
          (i === 0 ? " is-active" : "") +
          '" data-nav data-genre="' +
          escapeAttr(g.url) +
          '" type="button">' +
          escapeHtml(g.title) +
          "</button>"
        );
      }).join("");
      chips.dataset.ready = "1";
      $$("#genres-chips [data-genre]").forEach(function (btn) {
        btn.addEventListener("click", function () {
          $$("#genres-chips .chip").forEach(function (c) {
            c.classList.toggle("is-active", c === btn);
          });
          loadGenres(btn.getAttribute("data-genre"));
        });
      });
    }
    var target = url || (GENRES[0] && GENRES[0].url);
    var grid = $("#genres-grid");
    grid.innerHTML = '<div class="empty">Загрузка…</div>';
    try {
      var page = await HdRezka.getMoviesFromLink(target, 1);
      renderRow(grid, page.items || []);
    } catch (err) {
      grid.innerHTML = '<div class="empty">' + escapeHtml(err.message) + "</div>";
      toast(err.message);
    }
  }

  function syncCatalogChips() {
    $$("#catalog-kinds [data-kind]").forEach(function (btn) {
      btn.classList.toggle("is-active", btn.getAttribute("data-kind") === catalogKind);
    });
    $$("#catalog-filters [data-filter]").forEach(function (btn) {
      btn.classList.toggle("is-active", btn.getAttribute("data-filter") === catalogFilter);
    });
    var more = $("#catalog-more");
    if (more) more.hidden = !catalogHasNext;
  }

  async function loadCatalog(kind, filter, opts) {
    opts = opts || {};
    if (kind) catalogKind = kind;
    if (filter) catalogFilter = filter;
    if (!opts.append) catalogPage = 1;
    syncCatalogChips();
    var grid = $("#catalog-grid");
    if (!opts.append) grid.innerHTML = '<div class="empty">Загрузка…</div>';
    try {
      var page = await HdRezka.getCovers(catalogKind, {
        page: catalogPage,
        filter: catalogFilter,
        pageInfo: true,
      });
      catalogHasNext = !!page.hasNext;
      renderRow(grid, page.items || [], { append: !!opts.append });
      syncCatalogChips();
      if (!opts.append) TvNav.first($("#view-catalog"));
      else if ($("#catalog-more") && catalogHasNext) TvNav.focus($("#catalog-more"), true);
    } catch (err) {
      if (!opts.append) {
        grid.innerHTML = '<div class="empty">' + escapeHtml(err.message) + "</div>";
      }
      toast(err.message);
      catalogHasNext = false;
      syncCatalogChips();
    }
  }

  async function loadMoreCatalog() {
    if (!catalogHasNext) return;
    catalogPage += 1;
    await loadCatalog(null, null, { append: true });
  }

  async function runSearch(opts) {
    opts = opts || {};
    var q = $("#search-input").value.trim();
    if (!q) return;
    if (opts.full != null) searchFull = !!opts.full;
    if (!opts.append) {
      searchQuery = q;
      searchPage = 1;
    }
    var grid = $("#search-grid");
    var more = $("#search-more");
    if (!opts.append) {
      grid.innerHTML =
        '<div class="empty">' +
        (searchFull ? "Полный поиск" : "Ищем") +
        " «" +
        escapeHtml(searchQuery) +
        "»…</div>";
    }
    try {
      var page = await HdRezka.search(searchQuery, {
        full: searchFull || searchPage > 1,
        page: searchPage,
        pageInfo: true,
        preferCovers: searchFull,
      });
      searchHasNext = !!page.hasNext && (page.mode === "full" || searchFull);
      renderRow(grid, page.items || [], { append: !!opts.append });
      if (more) more.hidden = !searchHasNext;
      var cards = $$("#search-grid .card");
      if (!opts.append && cards[0]) TvNav.focus(cards[0], true);
      else if (opts.append && more && searchHasNext) TvNav.focus(more, true);
    } catch (err) {
      if (!opts.append) {
        grid.innerHTML = '<div class="empty">' + escapeHtml(err.message) + "</div>";
      }
      if (more) more.hidden = true;
      toast(err.message);
    }
  }

  async function loadMoreSearch() {
    if (!searchHasNext) return;
    searchPage += 1;
    searchFull = true;
    await runSearch({ append: true });
  }

  async function openPerson(url) {
    if (!url) {
      toast("Страница персоны недоступна");
      return;
    }
    personReturnView = appState.view === "person" ? personReturnView : appState.view;
    showView("person");
    $("#person-body").innerHTML = '<div class="empty">Загрузка…</div>';
    $("#person-grid").innerHTML = "";
    try {
      var person = await HdRezka.getPerson(url);
      appState.person = person;
      paintPerson();
    } catch (err) {
      $("#person-body").innerHTML = '<div class="empty">' + escapeHtml(err.message) + "</div>";
      toast(err.message);
    }
  }

  function paintPerson() {
    var p = appState.person;
    if (!p) return;
    var meta = [];
    if (p.dateOfBirth) meta.push("<div>Дата рождения: " + escapeHtml(p.dateOfBirth) + "</div>");
    if (p.placeOfBirth) meta.push("<div>Место рождения: " + escapeHtml(p.placeOfBirth) + "</div>");
    if (p.dateOfDeath) meta.push("<div>Дата смерти: " + escapeHtml(p.dateOfDeath) + "</div>");
    if (p.height) meta.push("<div>Рост: " + escapeHtml(p.height) + "</div>");
    var careers = (p.careers || [])
      .map(function (c) {
        return (
          '<button class="chip" data-nav data-career="' +
          escapeAttr(c.title) +
          '" type="button">' +
          escapeHtml(c.title) +
          " (" +
          (c.movies || []).length +
          ")</button>"
        );
      })
      .join("");
    $("#person-body").innerHTML =
      '<div class="person-head">' +
      '<div class="person-photo">' +
      (p.photo
        ? '<img src="' + escapeAttr(HdRezka.imgSrc(p.photoHq || p.photo)) + '" alt="">'
        : '<div class="card-fallback"></div>') +
      "</div><div>" +
      "<h1>" +
      escapeHtml(p.name || "") +
      "</h1>" +
      (p.nameOrig ? '<p class="orig">' + escapeHtml(p.nameOrig) + "</p>" : "") +
      (meta.length ? '<div class="person-meta">' + meta.join("") + "</div>" : "") +
      (careers ? '<div class="chips person-careers" id="person-careers">' + careers + "</div>" : "") +
      "</div></div>";

    var movies = p.movies || [];
    if (p.careers && p.careers.length === 1) movies = p.careers[0].movies || movies;
    renderRow($("#person-grid"), movies);
    $$("#person-careers [data-career]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        var title = btn.getAttribute("data-career");
        var career = (p.careers || []).filter(function (c) {
          return c.title === title;
        })[0];
        $$("#person-careers .chip").forEach(function (c) {
          c.classList.toggle("is-active", c === btn);
        });
        renderRow($("#person-grid"), (career && career.movies) || []);
        var card = $$("#person-grid .card")[0];
        if (card) TvNav.focus(card, true);
      });
    });
    var firstCareer = $$("#person-careers .chip")[0];
    if (firstCareer) firstCareer.classList.add("is-active");
    TvNav.first($("#view-person"));
  }

  function renderHistory() {
    var items = historyList();
    renderRow($("#history-grid"), items);
    if (!items.length) {
      $("#history-grid").innerHTML = '<div class="empty">История пуста. Откройте любой тайтл — он появится здесь.</div>';
    }
  }

  async function openDetails(url) {
    if (!url) return;
    showView("details");
    $("#details-body").innerHTML = '<div class="empty">Загрузка…</div>';
    try {
      var video = await HdRezka.getVideo(url);
      appState.details = video;
      rememberHistory(video);
      var regularTr = (video.translators || []).filter(function (t) {
        return !t.premium;
      })[0];
      appState.selectedTranslator = (regularTr || video.translators[0] || {}).id || null;
      appState.selectedSeason = video.seasons[0] ? video.seasons[0].id : "1";
      var eps = (video.episodes[appState.selectedSeason] || [])[0];
      appState.selectedEpisode = eps ? eps.id : "1";
      if (video.isSeries && appState.selectedTranslator) {
        try {
          var pack = await HdRezka.getEpisodes(video, appState.selectedTranslator);
          video.seasons = pack.seasons.length ? pack.seasons : video.seasons;
          video.episodes = pack.episodes;
          if (video.seasons[0]) appState.selectedSeason = video.seasons[0].id;
          var firstEp = (video.episodes[appState.selectedSeason] || [])[0];
          if (firstEp) appState.selectedEpisode = firstEp.id;
        } catch (err) {}
      }
      paintDetails();
    } catch (err) {
      $("#details-body").innerHTML = '<div class="empty">' + escapeHtml(err.message) + "</div>";
      toast(err.message);
    }
  }

  function chipRow(items, selected, attrName, extraClass) {
    return items
      .map(function (item) {
        var id = item.id || item;
        var title = item.title || item.name || item;
        var on = String(id) === String(selected);
        return (
          '<button class="chip' +
          (extraClass ? " " + extraClass : "") +
          (on ? " is-active" : "") +
          '" data-nav type="button" data-' +
          attrName +
          '="' +
          escapeAttr(id) +
          '">' +
          escapeHtml(title) +
          (item && item.premium ? " ★" : "") +
          "</button>"
        );
      })
      .join("");
  }

  function ratingStat(label, rating) {
    if (!rating || rating.value == null || isNaN(rating.value)) return "";
    return (
      '<div class="stat"><span class="stat-value">' +
      escapeHtml(String(rating.value)) +
      (rating.votes ? " (" + escapeHtml(formatVotes(rating.votes)) + ")" : "") +
      '</span><span class="stat-label">' +
      escapeHtml(label) +
      "</span></div>"
    );
  }

  function paintDetails() {
    var v = appState.details;
    if (!v) return;
    var cover = v.coverHq || v.cover;
    var bannerSrc = cover || v.backdrop;
    var cont = continueMap()[v.url];
    var tr = translatorById(appState.selectedTranslator);
    var seasons = v.seasons || [];
    var episodes = (v.episodes && v.episodes[appState.selectedSeason]) || [];
    var listsPreview = (v.lists || []).slice(0, 2).map(function (x) {
      return x.title + (x.place ? " (" + x.place + ")" : "");
    }).join(", ");
    var collectionsPreview = (v.collections || []).slice(0, 3).map(function (x) {
      return x.title;
    }).join(", ");
    var genresPreview = (v.genres || []).join(", ");
    var favOn = isFavorite(v.url);
    var cast = (v.people && v.people.length ? v.people : v.cast || []).slice(0, 40);

    $("#details-body").innerHTML =
      '<div class="details-page" id="details-page">' +
      '<section class="details-banner">' +
      (bannerSrc
        ? '<img class="details-banner-bg" src="' +
          escapeAttr(HdRezka.imgSrc(bannerSrc)) +
          '" alt="" aria-hidden="true">'
        : "") +
      '<div class="details-banner-shade" aria-hidden="true"></div>' +
      '<div class="details-banner-inner">' +
      '<div class="details-banner-head">' +
      '<div class="details-poster">' +
      (cover ? '<img src="' + escapeAttr(HdRezka.imgSrc(cover)) + '" alt="">' : '<div class="card-fallback"></div>') +
      "</div>" +
      '<div class="details-actions">' +
      '<button class="btn watch" data-nav id="btn-watch" type="button">▶ Смотреть</button>' +
      (cont
        ? '<button class="btn ghost" data-nav id="btn-continue" type="button">▶ Продолжить *</button>'
        : "") +
      '<button class="btn icon' +
      (favOn ? " is-on" : "") +
      '" data-nav id="btn-fav" type="button" title="В избранное">' +
      (favOn ? "★" : "☆") +
      "</button>" +
      "</div>" +
      (cont
        ? '<p class="details-continue-hint">* Продолжить смотреть ' +
          escapeHtml((translatorById(cont.translator) || {}).name || "") +
          " с " +
          escapeHtml(formatTime(cont.t)) +
          "</p>"
        : "") +
      '<div class="details-titles"><h1>' +
      escapeHtml(v.name) +
      "</h1>" +
      (v.original ? '<p class="orig">' + escapeHtml(v.original) + "</p>" : "") +
      "</div></div>" +
      '<div class="details-stats">' +
      ratingStat("КиноПоиск", v.ratings && v.ratings.kinopoisk) +
      ratingStat("IMDb", v.ratings && v.ratings.imdb) +
      ratingStat(
        "HDrezka",
        v.ratings && v.ratings.hdrezka
          ? v.ratings.hdrezka
          : v.rating != null
            ? { value: v.rating, votes: v.votes }
            : null
      ) +
      (v.duration
        ? '<div class="stat"><span class="stat-value is-neutral">' +
          escapeHtml(v.duration) +
          '</span><span class="stat-label">Длительность</span></div>'
        : "") +
      (v.age
        ? '<div class="stat"><span class="stat-value is-neutral">' +
          escapeHtml(v.age) +
          '</span><span class="stat-label">Возраст</span></div>'
        : "") +
      (v.year
        ? '<div class="stat"><span class="stat-value is-neutral">' +
          escapeHtml(v.year) +
          '</span><span class="stat-label">Год выхода</span></div>'
        : "") +
      "</div>" +
      '<div class="details-meta">' +
      (v.slogan ? "<div>Слоган: «" + escapeHtml(v.slogan) + "»</div>" : "") +
      (v.releaseDate ? "<div>Дата выхода: " + escapeHtml(v.releaseDate) + "</div>" : "") +
      ((v.countries || []).length
        ? "<div>Страна: " +
          v.countries
            .map(function (c) {
              return '<span class="badge">' + escapeHtml(c) + "</span>";
            })
            .join("") +
          "</div>"
        : "") +
      (genresPreview
        ? "<div>Жанр: " +
          escapeHtml(genresPreview) +
          ((v.genres || []).length > 3
            ? ' <button class="linkish" data-nav type="button" data-expand="genres">Нажмите, чтобы развернуть</button>'
            : "") +
          "</div>"
        : "") +
      (listsPreview
        ? "<div>Входит в списки: " +
          escapeHtml(listsPreview) +
          ((v.lists || []).length > 2
            ? ' <button class="linkish" data-nav type="button" data-expand="lists">Нажмите, чтобы развернуть</button>'
            : "") +
          "</div>"
        : "") +
      (collectionsPreview
        ? "<div>Из серии: " +
          escapeHtml(collectionsPreview) +
          ((v.collections || []).length > 3
            ? ' <button class="linkish" data-nav type="button" data-expand="collections">Нажмите, чтобы развернуть</button>'
            : "") +
          "</div>"
        : "") +
      "</div>" +
      '<div class="details-panel" id="expand-genres" hidden><h3>Жанры</h3><ul>' +
      (v.genres || [])
        .map(function (g) {
          return "<li>" + escapeHtml(g) + "</li>";
        })
        .join("") +
      "</ul></div>" +
      '<div class="details-panel" id="expand-lists" hidden><h3>Входит в списки</h3><ul>' +
      (v.lists || [])
        .map(function (x) {
          return "<li>" + escapeHtml(x.title) + (x.place ? " (" + escapeHtml(x.place) + ")" : "") + "</li>";
        })
        .join("") +
      "</ul></div>" +
      '<div class="details-panel" id="expand-collections" hidden><h3>Из серии</h3><ul>' +
      (v.collections || [])
        .map(function (x) {
          return "<li>" + escapeHtml(x.title) + "</li>";
        })
        .join("") +
      "</ul></div>" +
      "</div></section>" +
      '<div class="details-more">' +
      '<div class="details-split">' +
      '<div class="details-card"><h2>Описание</h2><p class="desc">' +
      escapeHtml(v.description || "Описание недоступно.") +
      "</p></div>" +
      (v.hasTrailer
        ? '<div class="details-card"><h2>Трейлер</h2>' +
          '<button class="trailer-btn" data-nav id="btn-trailer" type="button">' +
          '<img id="trailer-thumb" src="' +
          escapeAttr(HdRezka.imgSrc(cover)) +
          '" alt="">' +
          '<span class="play">▶</span></button></div>'
        : "") +
      "</div>" +
      (cast.length
        ? '<h2 class="details-section-title">Актерский состав и съемочная группа</h2>' +
          '<div class="cast-carousel' +
          (cast.length > 6 ? " is-scrollable" : "") +
          '"><div class="cast-row" id="cast-row">' +
          cast
            .map(function (p) {
              return (
                '<button class="cast-card" data-nav type="button" data-person-url="' +
                escapeAttr(p.url || "") +
                '" data-person="' +
                escapeAttr(p.name) +
                '">' +
                (p.photo
                  ? '<img src="' + escapeAttr(HdRezka.imgSrc(p.photo)) + '" alt="">'
                  : '<div class="cast-fallback"></div>') +
                "<strong>" +
                escapeHtml(p.name) +
                "</strong><span>" +
                escapeHtml(p.job || "") +
                "</span></button>"
              );
            })
            .join("") +
          "</div></div>"
        : "") +
      (v.isSeries
        ? '<div class="details-card" style="margin-bottom:18px"><h2>Сезон / серия</h2>' +
          '<div class="chips" id="se-list" style="margin-bottom:10px">' +
          chipRow(seasons, appState.selectedSeason, "se") +
          '</div><div class="chips" id="ep-list">' +
          chipRow(episodes, appState.selectedEpisode, "ep") +
          "</div></div>"
        : "") +
      '<div class="details-bottom-actions">' +
      '<button class="btn ghost" data-nav id="btn-back-home" type="button">← Назад</button>' +
      (tr ? '<span class="details-continue-hint">Озвучка: ' + escapeHtml(tr.name) + "</span>" : "") +
      "</div></div></div>";

    $("#btn-watch").addEventListener("click", function () {
      openTranslatorModal(false);
    });
    if ($("#btn-continue")) {
      $("#btn-continue").addEventListener("click", function () {
        if (cont && cont.translator) appState.selectedTranslator = cont.translator;
        if (cont && cont.season) appState.selectedSeason = cont.season;
        if (cont && cont.episode) appState.selectedEpisode = cont.episode;
        openTranslatorModal(true);
      });
    }
    $("#btn-fav").addEventListener("click", function () {
      var on = toggleFavorite(v);
      toast(on ? "Добавлено в избранное" : "Убрано из избранного");
      paintDetails();
    });
    if ($("#btn-trailer")) {
      $("#btn-trailer").addEventListener("click", openTrailerModal);
      prefetchTrailerThumb(v);
    }
    $$("[data-expand]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        var id = btn.getAttribute("data-expand");
        var panel = $("#expand-" + id);
        if (!panel) return;
        panel.hidden = !panel.hidden;
        btn.textContent = panel.hidden ? "Нажмите, чтобы развернуть" : "Свернуть";
      });
    });
    $$("#cast-row [data-person]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        var personUrl = btn.getAttribute("data-person-url");
        if (personUrl) openPerson(personUrl);
        else toast(btn.getAttribute("data-person"));
      });
      btn.addEventListener("focus", function () {
        var row = $("#cast-row");
        if (!row) return;
        var er = btn.getBoundingClientRect();
        var pr = row.getBoundingClientRect();
        var pad = 32;
        if (er.left < pr.left + pad) {
          row.scrollBy({ left: er.left - pr.left - pad, behavior: "smooth" });
        } else if (er.right > pr.right - pad) {
          row.scrollBy({ left: er.right - pr.right + pad, behavior: "smooth" });
        }
      });
    });
    var castRow = $("#cast-row");
    if (castRow) {
      var carousel = castRow.parentElement;
      var syncCastEdges = function () {
        if (!carousel || !carousel.classList.contains("is-scrollable")) return;
        var max = castRow.scrollWidth - castRow.clientWidth - 2;
        carousel.classList.toggle("has-left", castRow.scrollLeft > 8);
        carousel.classList.toggle("has-right", castRow.scrollLeft < max);
      };
      castRow.addEventListener("scroll", syncCastEdges, { passive: true });
      syncCastEdges();
    }
    $$("#se-list [data-se]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        appState.selectedSeason = btn.getAttribute("data-se");
        var first = (v.episodes[appState.selectedSeason] || [])[0];
        appState.selectedEpisode = first ? first.id : appState.selectedEpisode;
        paintDetails();
      });
    });
    $$("#ep-list [data-ep]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        appState.selectedEpisode = btn.getAttribute("data-ep");
        paintDetails();
      });
    });
    $("#btn-back-home").addEventListener("click", function () {
      showView("discover");
    });
    TvNav.focus($("#btn-watch"), true);
  }

  async function prefetchTrailerThumb(v) {
    try {
      var trailer = await HdRezka.getTrailer(v.id);
      if (trailer.thumb && $("#trailer-thumb")) {
        $("#trailer-thumb").src = trailer.thumb;
      }
      v._trailer = trailer;
    } catch (err) {}
  }

  async function openTrailerModal() {
    var v = appState.details;
    if (!v) return;
    try {
      var trailer = v._trailer || (await HdRezka.getTrailer(v.id));
      v._trailer = trailer;
      if (!trailer.youtubeId && !trailer.html) {
        toast("Трейлер недоступен");
        return;
      }
      openModal(
        "trailer",
        "<h2>Трейлер</h2>" +
          (trailer.youtubeId
            ? '<iframe class="trailer-frame" src="https://www.youtube.com/embed/' +
              escapeAttr(trailer.youtubeId) +
              '?autoplay=1&rel=0" allow="autoplay; encrypted-media" allowfullscreen></iframe>'
            : '<div class="desc">' + escapeHtml(stripHtmlLite(trailer.html)) + "</div>") +
          '<div class="modal-actions"><button class="btn ghost" data-nav id="modal-close" type="button">Закрыть</button></div>'
      );
      $("#modal-close").addEventListener("click", function () {
        closeModal();
      });
    } catch (err) {
      toast(err.message);
    }
  }

  function stripHtmlLite(html) {
    return String(html || "").replace(/<[^>]+>/g, " ").replace(/\s+/g, " ").trim();
  }

  function openTranslatorModal(fromContinue) {
    var v = appState.details;
    if (!v) return;
    var html =
      "<h2>Выберите озвучку</h2>" +
      '<div class="voice-grid" id="modal-tr-list">' +
      chipRow(v.translators || [], appState.selectedTranslator, "tr") +
      "</div>" +
      '<p class="modal-hint">ⓘ Нажмите еще раз по уже выбранной озвучке, чтобы сразу перейти к кнопке «Смотреть»</p>' +
      (v.isSeries
        ? '<div class="chips" id="modal-se-list" style="margin-bottom:10px">' +
          chipRow(v.seasons || [], appState.selectedSeason, "se") +
          '</div><div class="chips" id="modal-ep-list" style="margin-bottom:14px">' +
          chipRow((v.episodes && v.episodes[appState.selectedSeason]) || [], appState.selectedEpisode, "ep") +
          "</div>"
        : "") +
      '<div class="modal-actions">' +
      '<button class="btn watch" data-nav id="modal-watch" type="button">▶ Смотреть</button>' +
      '<button class="btn ghost" data-nav id="modal-cancel" type="button">Отмена</button>' +
      "</div>";
    openModal("translators", html, "#modal-watch");
    wireTranslatorModal(fromContinue);
  }

  function wireTranslatorModal(fromContinue) {
    var v = appState.details;
    $$("#modal-tr-list [data-tr]").forEach(function (btn) {
      btn.addEventListener("click", async function () {
        var tid = btn.getAttribute("data-tr");
        if (String(tid) === String(appState.selectedTranslator)) {
          prepareAndPlay(fromContinue);
          return;
        }
        appState.selectedTranslator = tid;
        if (v.isSeries) {
          try {
            var pack = await HdRezka.getEpisodes(v, tid);
            v.seasons = pack.seasons.length ? pack.seasons : v.seasons;
            v.episodes = pack.episodes;
            if (v.seasons[0]) appState.selectedSeason = v.seasons[0].id;
            var first = (v.episodes[appState.selectedSeason] || [])[0];
            if (first) appState.selectedEpisode = first.id;
          } catch (err) {
            toast(err.message);
          }
        }
        openTranslatorModal(fromContinue);
      });
    });
    $$("#modal-se-list [data-se]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        appState.selectedSeason = btn.getAttribute("data-se");
        var first = (v.episodes[appState.selectedSeason] || [])[0];
        appState.selectedEpisode = first ? first.id : appState.selectedEpisode;
        openTranslatorModal(fromContinue);
      });
    });
    $$("#modal-ep-list [data-ep]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        appState.selectedEpisode = btn.getAttribute("data-ep");
        openTranslatorModal(fromContinue);
      });
    });
    $("#modal-watch").addEventListener("click", function () {
      prepareAndPlay(fromContinue);
    });
    $("#modal-cancel").addEventListener("click", function () {
      closeModal();
    });
  }

  async function prepareAndPlay(fromContinue) {
    var v = appState.details;
    if (!v || !appState.selectedTranslator) {
      toast("Выберите озвучку");
      return;
    }
    closeModal(true);
    toast("Получение потока…");
    try {
      var stream = await HdRezka.getStream(v, {
        translatorId: appState.selectedTranslator,
        season: appState.selectedSeason,
        episode: appState.selectedEpisode,
      });
      appState.stream = stream;
      appState.selectedQuality = stream.best;
      appState.selectedSub = stream.subtitles[0] ? stream.subtitles[0].url : null;
      openQualityModal(fromContinue);
    } catch (err) {
      toast(err.message);
      openTranslatorModal(fromContinue);
    }
  }

  function openQualityModal(fromContinue) {
    var stream = appState.stream;
    if (!stream) return;
    var html =
      "<h2>Выберите качество</h2>" +
      '<div class="modal-list" id="modal-q-list">' +
      stream.qualities
        .map(function (q) {
          return (
            '<button class="chip' +
            (q === appState.selectedQuality ? " is-active" : "") +
            '" data-nav data-q="' +
            escapeAttr(q) +
            '" type="button">' +
            escapeHtml(q) +
            (stream.formats[q].isPremium ? " ★" : "") +
            (stream.formats[q].isTeaser ? " · заставка" : "") +
            "</button>"
          );
        })
        .join("") +
      "</div>" +
      '<div class="modal-actions" style="margin-top:18px">' +
      '<button class="btn watch" data-nav id="modal-play" type="button">▶ Смотреть</button>' +
      '<button class="btn ghost" data-nav id="modal-q-back" type="button">Назад</button>' +
      "</div>";
    openModal("quality", html, "#modal-play");
    $$("#modal-q-list [data-q]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        appState.selectedQuality = btn.getAttribute("data-q");
        openQualityModal(fromContinue);
      });
    });
    $("#modal-play").addEventListener("click", function () {
      closeModal(true);
      startPlayback(fromContinue);
    });
    $("#modal-q-back").addEventListener("click", function () {
      openTranslatorModal(fromContinue);
    });
  }

  async function startPlayback(fromContinue) {
    var v = appState.details;
    var stream = appState.stream;
    if (!v || !stream) return;
    showView("player", { keepFocus: true });
    mountPlayer();
    if (fromContinue) {
      var cont = continueMap()[v.url];
      if (cont && cont.t > 5) {
        var video = $("#player-video");
        var seek = function () {
          video.currentTime = cont.t;
          video.removeEventListener("loadedmetadata", seek);
        };
        video.addEventListener("loadedmetadata", seek);
      }
    }
  }

  function mountPlayer() {
    var v = appState.details;
    var stream = appState.stream;
    var format = stream.formats[appState.selectedQuality] || stream.formats[stream.best];
    var url = format ? format.url || format.mp4 || format.hls : "";
    var video = $("#player-video");
    $("#player-title").textContent = v.name + (v.isSeries ? " · s" + appState.selectedSeason + "e" + appState.selectedEpisode : "");
    $("#quality-list").innerHTML = stream.qualities
      .map(function (q) {
        return (
          '<button class="chip' +
          (q === appState.selectedQuality ? " is-active" : "") +
          '" data-nav data-q="' +
          escapeAttr(q) +
          '" type="button">' +
          escapeHtml(q) +
          (stream.formats[q].isPremium ? " ★" : "") +
          "</button>"
        );
      })
      .join("");
    $("#subs-list").innerHTML =
      '<button class="chip' +
      (!appState.selectedSub ? " is-active" : "") +
      '" data-nav data-sub="" type="button">Выкл</button>' +
      stream.subtitles
        .map(function (s) {
          return (
            '<button class="chip' +
            (appState.selectedSub === s.url ? " is-active" : "") +
            '" data-nav data-sub="' +
            escapeAttr(s.url) +
            '" type="button">' +
            escapeHtml(s.title) +
            "</button>"
          );
        })
        .join("");
    $$("#quality-list [data-q]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        var t = video.currentTime;
        appState.selectedQuality = btn.getAttribute("data-q");
        mountPlayer();
        video.currentTime = t;
      });
    });
    $$("#subs-list [data-sub]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        appState.selectedSub = btn.getAttribute("data-sub") || null;
        applySubs(video);
        $$("#subs-list .chip").forEach(function (c) {
          c.classList.toggle("is-active", c === btn);
        });
      });
    });
    $("#btn-player-back").onclick = leavePlayer;
    $("#btn-next-ep").hidden = !v.isSeries;
    $("#btn-next-ep").onclick = playNextEpisode;
    video.src = HdRezka.streamSrc(url);
    applySubs(video);
    video.play().catch(function () {
      $("#player-status").textContent = "Нажмите Cross, чтобы начать";
    });
    $("#player-status").textContent = stream.previewOnly
      ? "Это заставка HDRezka. Полный фильм для этой озвучки только с Premium."
      : appState.selectedQuality || "";
    paintPlayerTime();
    showPlayerChrome();
    TvNav.focus($("#btn-player-play"), false);
  }

  function leavePlayer() {
    var video = $("#player-video");
    if (video) video.pause();
    showView("details");
  }

  function fmtClock(seconds) {
    if (!isFinite(seconds) || seconds < 0) seconds = 0;
    seconds = Math.floor(seconds);
    var h = Math.floor(seconds / 3600);
    var m = Math.floor((seconds % 3600) / 60);
    var s = seconds % 60;
    var ss = (s < 10 ? "0" : "") + s;
    if (h) return h + ":" + (m < 10 ? "0" : "") + m + ":" + ss;
    return m + ":" + ss;
  }

  function paintPlayerTime() {
    var video = $("#player-video");
    var dur = video && isFinite(video.duration) ? video.duration : 0;
    var now = video ? video.currentTime || 0 : 0;
    var pct = dur ? (now / dur) * 100 : 0;
    var bar = $("#seek-bar");
    var knob = $("#seek-knob");
    if (bar) bar.style.width = pct + "%";
    if (knob) knob.style.left = pct + "%";
    var time = $("#player-time");
    var total = $("#player-duration");
    if (time) time.textContent = fmtClock(now);
    if (total) total.textContent = fmtClock(dur);
    var play = $("#btn-player-play");
    if (play && video) play.textContent = video.paused ? "▶" : "❚❚";
  }

  function showPlayerChrome() {
    var ui = $("#player-ui");
    if (!ui) return;
    ui.classList.remove("is-hidden");
    clearTimeout(playerChromeTimer);
    var video = $("#player-video");
    if (video && !video.paused && !playerScrubbing) {
      playerChromeTimer = setTimeout(function () {
        if (appState.view === "player" && !video.paused && !playerScrubbing) ui.classList.add("is-hidden");
      }, 3200);
    }
  }

  function flashSeek(delta) {
    var el = $("#player-seek-flash");
    if (!el) return;
    el.hidden = false;
    el.textContent = (delta > 0 ? "+" : "") + delta + " с";
    clearTimeout(seekFlashTimer);
    seekFlashTimer = setTimeout(function () {
      el.hidden = true;
    }, 700);
  }

  function seekBy(delta) {
    var video = $("#player-video");
    if (!video) return;
    var dur = isFinite(video.duration) ? video.duration : Infinity;
    var next = video.currentTime + delta;
    if (next < 0) next = 0;
    if (next > dur) next = dur;
    video.currentTime = next;
    paintPlayerTime();
    flashSeek(delta);
    showPlayerChrome();
  }

  function seekRatio(ratio) {
    var video = $("#player-video");
    if (!video || !isFinite(video.duration) || !video.duration) return;
    var x = Math.max(0, Math.min(1, ratio));
    video.currentTime = x * video.duration;
    paintPlayerTime();
    showPlayerChrome();
  }

  function togglePlayer() {
    var video = $("#player-video");
    if (!video) return;
    if (video.paused) video.play();
    else video.pause();
    paintPlayerTime();
    showPlayerChrome();
  }

  function applySubs(video) {
    $$("track", video).forEach(function (t) {
      t.remove();
    });
    if (!appState.selectedSub) return;
    var track = document.createElement("track");
    track.kind = "subtitles";
    track.label = "subs";
    track.srclang = "ru";
    track.src = "/stream?url=" + encodeURIComponent(HdRezka.absUrl(appState.selectedSub));
    track.default = true;
    video.appendChild(track);
  }

  async function fetchStreamAndPlay() {
    var v = appState.details;
    if (!v || !appState.selectedTranslator) return;
    try {
      var stream = await HdRezka.getStream(v, {
        translatorId: appState.selectedTranslator,
        season: appState.selectedSeason,
        episode: appState.selectedEpisode,
      });
      appState.stream = stream;
      if (!stream.formats[appState.selectedQuality]) {
        appState.selectedQuality = stream.best;
      }
      appState.selectedSub = stream.subtitles[0] ? stream.subtitles[0].url : null;
      await startPlayback(false);
    } catch (err) {
      toast(err.message);
    }
  }

  async function playNextEpisode() {
    var v = appState.details;
    var list = (v.episodes && v.episodes[appState.selectedSeason]) || [];
    var idx = -1;
    list.forEach(function (ep, i) {
      if (String(ep.id) === String(appState.selectedEpisode)) idx = i;
    });
    if (idx >= 0 && idx + 1 < list.length) {
      appState.selectedEpisode = list[idx + 1].id;
      await fetchStreamAndPlay();
      return;
    }
    var seasons = v.seasons || [];
    var sidx = -1;
    seasons.forEach(function (s, i) {
      if (String(s.id) === String(appState.selectedSeason)) sidx = i;
    });
    if (sidx >= 0 && sidx + 1 < seasons.length) {
      appState.selectedSeason = seasons[sidx + 1].id;
      var first = (v.episodes[appState.selectedSeason] || [])[0];
      if (first) {
        appState.selectedEpisode = first.id;
        await fetchStreamAndPlay();
        return;
      }
    }
    toast("Это последняя серия");
  }

  function wirePlayerProgress() {
    var video = $("#player-video");
    video.addEventListener("timeupdate", function () {
      if (!appState.details) return;
      var key = appState.details.url;
      saveContinue(key, {
        t: video.currentTime,
        d: video.duration || 0,
        season: appState.selectedSeason,
        episode: appState.selectedEpisode,
        translator: appState.selectedTranslator,
      });
      paintPlayerTime();
    });
    video.addEventListener("play", function () {
      paintPlayerTime();
      showPlayerChrome();
    });
    video.addEventListener("pause", function () {
      paintPlayerTime();
      showPlayerChrome();
    });
    video.addEventListener("click", function () {
      showPlayerChrome();
    });
    video.addEventListener("ended", function () {
      if (appState.details && appState.details.isSeries) playNextEpisode();
    });
    var bar = $("#player-seek");
    function ratioFromEvent(event) {
      var rect = bar.getBoundingClientRect();
      var x = event.clientX;
      if (event.touches && event.touches[0]) x = event.touches[0].clientX;
      if (!rect.width) return 0;
      return Math.max(0, Math.min(1, (x - rect.left) / rect.width));
    }
    bar.addEventListener("pointerdown", function (event) {
      playerScrubbing = true;
      try { bar.setPointerCapture(event.pointerId); } catch (err) {}
      seekRatio(ratioFromEvent(event));
    });
    bar.addEventListener("pointermove", function (event) {
      if (!playerScrubbing) return;
      seekRatio(ratioFromEvent(event));
    });
    function endScrub() {
      if (!playerScrubbing) return;
      playerScrubbing = false;
      showPlayerChrome();
    }
    bar.addEventListener("pointerup", endScrub);
    bar.addEventListener("pointercancel", endScrub);
    $("#btn-player-play").addEventListener("click", togglePlayer);
  }

  function daysLabel(n) {
    var n10 = n % 10;
    var n100 = n % 100;
    var word = "дней";
    if (n10 === 1 && n100 !== 11) word = "день";
    else if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14)) word = "дня";
    return n + " " + word;
  }

  function renderProfile(account) {
    profileAccount = account;
    var card = $("#profile-card");
    var library = $("#profile-library");
    var auth = $("#profile-auth");
    if (!account || !account.loggedIn) {
      library.hidden = true;
      auth.hidden = false;
      $("#profile-library-body").innerHTML = "";
      card.innerHTML =
        '<div class="profile-head">' +
        '<div class="profile-avatar" aria-hidden="true">?</div>' +
        "<div><h2>Вы не вошли в аккаунт</h2>" +
        "<p>Авторизуйтесь, чтобы видеть срок подписки, закладки и новые серии.</p></div></div>";
      return;
    }
    auth.hidden = true;
    library.hidden = false;
    var initial = (account.name || "А").trim().charAt(0).toUpperCase();
    var avatar = account.avatar
      ? '<img src="' + escapeAttr(HdRezka.imgSrc(account.avatar)) + '" alt="">'
      : escapeHtml(initial);
    var subValue = "Подписка не активна";
    var subNote = "Premium открывает 4K, 1080p и премиум-озвучки на зеркале.";
    var premClass = "";
    if (account.days != null) {
      subValue = daysLabel(account.days);
      subNote = account.premiumText && account.premiumText !== String(account.days) ? account.premiumText : "Осталось до окончания подписки";
      premClass = " is-prem";
    } else if (account.premium) {
      subValue = account.premiumText || "Premium";
      subNote = "Подписка активна";
      premClass = " is-prem";
    }
    card.innerHTML =
      '<div class="profile-head">' +
      '<div class="profile-avatar">' +
      avatar +
      "</div><div><h2>С возвращением, " +
      escapeHtml(account.name || "аккаунт") +
      "!</h2>" +
      (account.email ? "<p>" + escapeHtml(account.email) + "</p>" : "") +
      '<p>ID ' +
      escapeHtml(account.id) +
      "</p></div></div>" +
      '<div class="profile-stats">' +
      '<div class="profile-stat' +
      premClass +
      '"><span>До окончания подписки</span><b>' +
      escapeHtml(subValue) +
      "</b></div>" +
      '<div class="profile-stat"><span>Статус</span><b>' +
      escapeHtml(account.premium || account.days != null ? "Premium" : "Без подписки") +
      "</b><span>" +
      escapeHtml(subNote) +
      "</span></div></div>" +
      '<div class="settings-actions"><button class="btn" id="profile-logout" data-nav type="button">Выйти</button></div>';
  }

  async function loadProfile() {
    var card = $("#profile-card");
    if (!card.innerHTML.trim()) card.innerHTML = '<div class="empty">Загрузка…</div>';
    try {
      renderProfile(await HdRezka.getAccount());
    } catch (err) {
      card.innerHTML = '<div class="empty">' + escapeHtml(err.message || "Не удалось загрузить профиль") + "</div>";
      toast(err.message);
    }
  }

  async function loadProfileLibrary(kind) {
    $$("[data-library]").forEach(function (btn) {
      btn.classList.toggle("is-active", btn.getAttribute("data-library") === kind);
    });
    var body = $("#profile-library-body");
    body.className = "";
    body.innerHTML = '<div class="empty">Загрузка…</div>';
    try {
      if (kind === "updates") {
        var groups = (profileAccount && profileAccount.updates) || [];
        if (!groups.length && profileAccount && profileAccount.loggedIn) {
          profileAccount = await HdRezka.getAccount();
          groups = profileAccount.updates || [];
        }
        if (!groups.length) {
          body.innerHTML = '<div class="empty">Нет обновлений за последние 6 дней. Начните смотреть незавершённый сериал, и новые серии появятся здесь.</div>';
          return;
        }
        body.innerHTML =
          '<div class="profile-updates">' +
          groups
            .map(function (group) {
              return (
                "<section><h3>" +
                escapeHtml(group.date || "Новые серии") +
                "</h3>" +
                group.items
                  .map(function (item) {
                    return (
                      '<button class="profile-update" data-nav data-url="' +
                      escapeAttr(item.url) +
                      '" type="button">' +
                      escapeHtml(item.title) +
                      "</button>"
                    );
                  })
                  .join("") +
                "</section>"
              );
            })
            .join("") +
          "</div>";
        $$(".profile-update", body).forEach(function (btn) {
          btn.addEventListener("click", function () {
            openDetails(btn.getAttribute("data-url"));
          });
        });
        return;
      }
      var page = await HdRezka.getMoviesFromLink(kind === "favorites" ? "/favorites/" : "/continue/");
      if (!page.items || !page.items.length) {
        body.innerHTML =
          '<div class="empty">' +
          (kind === "favorites"
            ? "Нет закладок"
            : "Начните смотреть фильм или сериал, и он появится здесь.") +
          "</div>";
        return;
      }
      body.className = "grid poster-grid";
      renderRow(body, page.items);
    } catch (err) {
      body.innerHTML = '<div class="empty">' + escapeHtml(err.message || "Ошибка") + "</div>";
      toast(err.message);
    }
  }

  async function loadSettings() {
    var cfg = await HdRezka.loadConfig();
    $("#cfg-origin").value = cfg.origin || "";
    var mirrors = cfg.mirrors || [];
    $("#mirror-list").innerHTML = mirrors
      .map(function (m) {
        return (
          '<button class="chip' +
          (m === cfg.origin ? " is-active" : "") +
          '" data-nav data-mirror="' +
          escapeAttr(m) +
          '" type="button">' +
          escapeHtml(m.replace(/^https?:\/\//, "")) +
          "</button>"
        );
      })
      .join("");
    $$("#mirror-list [data-mirror]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        $("#cfg-origin").value = btn.getAttribute("data-mirror");
        $$("#mirror-list [data-mirror]").forEach(function (chip) {
          chip.classList.toggle("is-active", chip === btn);
        });
      });
    });
    loadProfile();
  }

  async function saveSettings() {
    try {
      await HdRezka.saveConfig({
        origin: $("#cfg-origin").value.trim(),
      });
      toast("Настройки сохранены");
      loadDiscover();
    } catch (err) {
      toast(err.message);
    }
  }

  async function doLogin() {
    try {
      await HdRezka.login($("#cfg-email").value.trim(), $("#cfg-password").value);
      $("#cfg-password").value = "";
      toast("Вход выполнен");
      await loadProfile();
    } catch (err) {
      toast(err.message);
    }
  }

  async function doLogout() {
    try {
      await HdRezka.logout();
      profileAccount = null;
      toast("Вы вышли из аккаунта");
      await loadProfile();
    } catch (err) {
      toast(err.message);
    }
  }

  function switchTab(dir) {
    var idx = tabs.indexOf(appState.view);
    if (idx < 0) return;
    var next = tabs[(idx + dir + tabs.length) % tabs.length];
    openTab(next);
  }

  function openTab(name, kind) {
    if (kind) catalogKind = kind;
    if (name === "catalog") {
      showView("catalog");
      loadCatalog(catalogKind);
    } else if (name === "genres") {
      showView("genres");
      loadGenres();
    } else if (name === "mylist") {
      showView("mylist");
      renderMyList();
    } else if (name === "continue") {
      showView("continue");
      loadSiteList("continue");
    } else if (name === "bookmarks") {
      showView("bookmarks");
      loadSiteList("bookmarks");
    } else if (name === "history") {
      showView("history");
      renderHistory();
    } else if (name === "settings") {
      showView("settings");
      loadSettings();
    } else if (name === "search") {
      showView("search");
      TvNav.focus($("#search-input"), true);
    } else {
      showView("discover");
      if (!$("#home-films").children.length || $("#home-films .empty")) loadDiscover();
    }
  }

  function onKey(event) {
    if (event.altKey || event.metaKey || event.ctrlKey) return;
    var inField = event.target && (event.target.tagName === "INPUT" || event.target.tagName === "TEXTAREA");
    if (appState.view === "player" && $("#modal-root").hidden && !TvNav.match(TvNav.KEY.BACK, event)) {
      var wasHidden = $("#player-ui").classList.contains("is-hidden");
      showPlayerChrome();
      if (TvNav.match(TvNav.KEY.TAB_PREV, event) || event.key === "[") {
        event.preventDefault();
        seekBy(-10);
        return;
      }
      if (TvNav.match(TvNav.KEY.TAB_NEXT, event) || event.key === "]") {
        event.preventDefault();
        seekBy(10);
        return;
      }
      if (TvNav.match(TvNav.KEY.PLAY, event)) {
        event.preventDefault();
        if (!wasHidden) togglePlayer();
        return;
      }
      if (wasHidden) {
        event.preventDefault();
        return;
      }
    }
    if (TvNav.match(TvNav.KEY.BACK, event)) {
      event.preventDefault();
      if (!$("#modal-root").hidden) {
        if (modalState.kind === "quality") openTranslatorModal(false);
        else closeModal();
        return;
      }
      if (appState.view === "player") {
        $("#player-video").pause();
        showView("details");
      } else if (appState.view === "person") {
        if (personReturnView === "details" && appState.details) showView("details");
        else openTab(personReturnView === "search" ? "search" : "discover");
      } else if (appState.view === "details") showView("discover");
      else if (inField) event.target.blur();
      else openTab("discover");
      return;
    }
    if (inField && !TvNav.match(TvNav.KEY.ENTER, event) && !TvNav.match(TvNav.KEY.UP, event) && !TvNav.match(TvNav.KEY.DOWN, event)) {
      return;
    }
    var navRoot = modalRoot() || document;
    if (TvNav.match(TvNav.KEY.LEFT, event)) {
      event.preventDefault();
      TvNav.move("left", navRoot);
    } else if (TvNav.match(TvNav.KEY.RIGHT, event)) {
      event.preventDefault();
      TvNav.move("right", navRoot);
    } else if (TvNav.match(TvNav.KEY.UP, event)) {
      event.preventDefault();
      TvNav.move("up", navRoot);
    } else if (TvNav.match(TvNav.KEY.DOWN, event)) {
      event.preventDefault();
      TvNav.move("down", navRoot);
    } else if (TvNav.match(TvNav.KEY.ENTER, event)) {
      if (inField && event.target.id === "search-input") {
        event.preventDefault();
        runSearch({ full: false });
        return;
      }
      var el = document.activeElement;
      if (el && el !== document.body && typeof el.click === "function") {
        event.preventDefault();
        el.click();
      }
    } else if (TvNav.match(TvNav.KEY.TAB_NEXT, event) || event.key === "Tab" && !event.shiftKey && event.key === "Tab") {
      /* L1/R1 analogue via [ ] */
    }
    if (appState.view !== "player" && (event.key === "[" || event.code === "BracketLeft")) {
      event.preventDefault();
      switchTab(-1);
    }
    if (appState.view !== "player" && (event.key === "]" || event.code === "BracketRight")) {
      event.preventDefault();
      switchTab(1);
    }
  }

  function onPad(action, extra) {
    if (appState.view === "player" && $("#modal-root").hidden) {
      if (action === "activity") {
        playerJustRevealed = $("#player-ui").classList.contains("is-hidden");
        showPlayerChrome();
        return;
      }
      if (action === "back") {
        leavePlayer();
        return;
      }
      if (action === "tab-prev") {
        seekBy(-10);
        return;
      }
      if (action === "tab-next") {
        seekBy(10);
        return;
      }
      if (action === "touch") {
        seekRatio(typeof extra === "number" ? extra : 0);
        return;
      }
      if (action === "options") {
        showPlayerChrome();
        return;
      }
      if (playerJustRevealed) {
        playerJustRevealed = false;
        showPlayerChrome();
        return;
      }
      showPlayerChrome();
    }
    if (action === "activity") return;
    var navRoot = modalRoot() || document;
    if (action === "left") TvNav.move("left", navRoot);
    if (action === "right") TvNav.move("right", navRoot);
    if (action === "up") TvNav.move("up", navRoot);
    if (action === "down") TvNav.move("down", navRoot);
    if (action === "enter") {
      var el = document.activeElement;
      if (el && typeof el.click === "function") el.click();
    }
    if (action === "back") {
      onKey({ key: "Escape", preventDefault: function () {}, altKey: false, metaKey: false, ctrlKey: false, target: document.body });
    }
    if ((action === "tab-prev" || action === "tab-next") && extra && extra.repeat) return;
    if (action === "tab-prev" && $("#modal-root").hidden) switchTab(-1);
    if (action === "tab-next" && $("#modal-root").hidden) switchTab(1);
    if (action === "options" && $("#modal-root").hidden) openTab("search");
  }

  function boot() {
    $$(".tab").forEach(function (tab) {
      tab.addEventListener("click", function () {
        openTab(tab.getAttribute("data-view"), tab.getAttribute("data-kind"));
      });
    });
    $$(".icon-btn[data-view]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        openTab(btn.getAttribute("data-view"));
      });
    });
    $$("#home-filters [data-home]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        loadHomeRow(btn.getAttribute("data-home"));
      });
    });
    $$("#catalog-kinds [data-kind]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        loadCatalog(btn.getAttribute("data-kind"), catalogFilter);
      });
    });
    $$("#catalog-filters [data-filter]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        loadCatalog(catalogKind, btn.getAttribute("data-filter"));
      });
    });
    $("#catalog-more").addEventListener("click", loadMoreCatalog);
    $("#search-go").addEventListener("click", function () {
      runSearch({ full: false });
    });
    $("#search-full").addEventListener("click", function () {
      runSearch({ full: true });
    });
    $("#search-more").addEventListener("click", loadMoreSearch);
    $("#search-input").addEventListener("keydown", function (event) {
      if (event.key === "Enter") runSearch({ full: false });
    });
    $("#person-back").addEventListener("click", function () {
      if (personReturnView === "details" && appState.details) showView("details");
      else openTab(personReturnView === "search" ? "search" : "discover");
    });
    $("#cfg-save").addEventListener("click", saveSettings);
    $("#cfg-login").addEventListener("click", doLogin);
    $("#profile-card").addEventListener("click", function (event) {
      var node = event.target;
      while (node && node !== $("#profile-card")) {
        if (node.id === "profile-logout") {
          doLogout();
          return;
        }
        node = node.parentNode;
      }
    });
    $$("[data-library]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        loadProfileLibrary(btn.getAttribute("data-library"));
      });
    });
    $("#hero-play").addEventListener("click", function () {
      openDetails($("#hero-play").getAttribute("data-url"));
    });
    $("#hero-fav").addEventListener("click", function () {
      var url = $("#hero-fav").getAttribute("data-url");
      var slide = heroSlides[heroIndex] || { url: url, title: $("#hero-title").textContent, cover: "" };
      if (!slide.url) return;
      var on = toggleFavorite({
        url: slide.url,
        name: slide.title || slide.name,
        cover: slide.cover,
      });
      toast(on ? "Добавлено в мой список" : "Убрано из списка");
      paintHero(slide);
    });
    $("#history-clear").addEventListener("click", function () {
      writeJson(LS_HISTORY, []);
      renderHistory();
    });
    document.addEventListener("keydown", onKey);
    document.addEventListener("keydown", function (event) {
      if (event.key === "Triangle" || event.code === "KeyT" && event.altKey) {
        event.preventDefault();
        openTab("search");
      }
    });
    $("#modal-scrim").addEventListener("click", function () {
      closeModal();
    });
    wirePlayerProgress();
    formatClock();
    setInterval(formatClock, 15000);
    setInterval(function () {
      TvNav.pollGamepad(onPad);
    }, 80);
    HdRezka.loadConfig()
      .then(loadDiscover)
      .catch(function (err) {
        setBusy(false);
        toast(err.message || "Сервис недоступен");
      });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();
