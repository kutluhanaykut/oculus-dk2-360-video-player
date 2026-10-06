// "DK2'de aç": sends the current page to DK2 360 VR Player.
//
// Like the bookmarklet, it builds a dk2vr://open link with the page URL and
// the best video source found on the page. It also adds the site's cookies
// (including HttpOnly ones, which page scripts cannot see) and the browser's
// User-Agent, so sites that need a login or sit behind Cloudflare work.
// yt-dlp cannot read the cookies itself while Brave is running: the cookie
// database is locked.

// Windows passes the link to the player on its command line (32767 chars).
const MAX_LINK_LENGTH = 30000;

function toNetscapeLine(cookie) {
  const domain = cookie.hostOnly ? cookie.domain : (cookie.domain.startsWith('.') ? cookie.domain : '.' + cookie.domain);
  return [
    (cookie.httpOnly ? '#HttpOnly_' : '') + domain,
    cookie.hostOnly ? 'FALSE' : 'TRUE',
    cookie.path,
    cookie.secure ? 'TRUE' : 'FALSE',
    cookie.session ? 0 : Math.floor(cookie.expirationDate || 0),
    cookie.name,
    cookie.value,
  ].join('\t');
}

// Runs inside the page. VR players often keep the video out of plain sight:
// DL8/DeoVR use <dl8-video> with <source quality="4K"> children, others put
// the player in a shadow root or stream through blob: URLs. Collect every
// http(s) source from the DOM (shadow roots included) and from the media
// requests the page made, and return the best one with any projection
// format the player declares (e.g. STEREO_180_LR).
function findBestVideo() {
  const candidates = [];
  const add = (url, quality, format, rank) => {
    if (!/^https?:/i.test(url || '') || candidates.some((c) => c.url === url)) {
      return;
    }
    candidates.push({ url, quality, format, rank });
  };
  const qualityOf = (text) => {
    const value = String(text || '').toLowerCase();
    if (/8k|4320/.test(value)) return 4320;
    if (/6k|3240|3200|3072/.test(value)) return 3200;
    if (/5k|2880|2700/.test(value)) return 2880;
    if (/4k|2160|uhd/.test(value)) return 2160;
    const digits = value.match(/(\d{3,4})p?/);
    return digits ? Number(digits[1]) : 0;
  };
  const formatOf = (element) => {
    const holder = element.closest('[format], [projection], [data-projection], [data-format]');
    if (!holder) return '';
    return holder.getAttribute('format') || holder.getAttribute('projection')
      || holder.getAttribute('data-projection') || holder.getAttribute('data-format') || '';
  };
  const visit = (root) => {
    root.querySelectorAll('source').forEach((source) => {
      const label = source.getAttribute('quality') || source.getAttribute('label')
        || source.getAttribute('size') || source.getAttribute('res') || source.src;
      add(source.src, qualityOf(label), formatOf(source), 3);
    });
    root.querySelectorAll('video').forEach((video) => {
      add(video.currentSrc || video.src, qualityOf(video.videoHeight || video.currentSrc), formatOf(video), 2);
    });
    root.querySelectorAll('*').forEach((element) => {
      if (element.shadowRoot) visit(element.shadowRoot);
    });
  };
  visit(document);

  // Streams the page actually fetched: HLS/DASH manifests and media files.
  for (const entry of performance.getEntriesByType('resource')) {
    if (/\.(m3u8|mpd)(\?|$)/i.test(entry.name)) {
      add(entry.name, qualityOf(entry.name), '', 1);
    } else if (/\.(mp4|webm|mkv|mov)(\?|$)/i.test(entry.name)) {
      add(entry.name, qualityOf(entry.name), '', 0);
    }
  }

  // Previews and trailers are a common trap: rank them last.
  const isPreview = (url) => /preview|trailer|teaser|thumb|sample/i.test(url);
  candidates.sort((a, b) => (isPreview(a.url) - isPreview(b.url))
    || (b.quality - a.quality) || (b.rank - a.rank));
  const format = (candidates.find((c) => c.format) || {}).format || '';
  return candidates.length ? { url: candidates[0].url, format } : { url: '', format };
}

async function pageVideo(tabId) {
  try {
    const frames = await chrome.scripting.executeScript({
      target: { tabId, allFrames: true },
      world: 'MAIN',
      func: findBestVideo,
    });
    // Top frame first; an embedded player frame when the top has nothing.
    for (const frame of frames) {
      if (frame && frame.result && frame.result.url) {
        return frame.result;
      }
    }
  } catch (error) {
    // e.g. a page the extension may not script
  }
  return { url: '', format: '' };
}

async function cookiesFor(urls) {
  const seen = new Set();
  const lines = [];
  for (const url of urls) {
    for (const cookie of await chrome.cookies.getAll({ url })) {
      const key = `${cookie.domain}\t${cookie.path}\t${cookie.name}`;
      if (!seen.has(key)) {
        seen.add(key);
        lines.push(toNetscapeLine(cookie));
      }
    }
  }
  return lines.join('\n');
}

chrome.action.onClicked.addListener(async (tab) => {
  if (!tab.id || !/^https?:/i.test(tab.url || '')) {
    return;
  }
  const video = await pageVideo(tab.id);
  const urls = video.url ? [tab.url, video.url] : [tab.url];

  let link = 'dk2vr://open?url=' + encodeURIComponent(tab.url);
  if (video.url) {
    link += '&video=' + encodeURIComponent(video.url);
  }
  if (video.format) {
    link += '&projection=' + encodeURIComponent(video.format);
  }
  // Cloudflare binds its clearance cookie to the browser's User-Agent, so
  // the player has to send the same one.
  link += '&ua=' + encodeURIComponent(navigator.userAgent);
  const cookies = await cookiesFor(urls);
  const withCookies = cookies ? link + '&cookies=' + encodeURIComponent(cookies) : link;
  // Too many cookies for a command line: send the page without them.
  link = withCookies.length <= MAX_LINK_LENGTH ? withCookies : link;

  // Navigating to an external scheme hands it to Windows (Brave asks once)
  // and leaves the page where it is.
  await chrome.tabs.update(tab.id, { url: link });
});
