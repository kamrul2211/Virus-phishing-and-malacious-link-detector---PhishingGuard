importScripts('engine.js');

let evaluateUrlCpp = null;
let addDomainCpp = null;
let removeDomainCpp = null;

// চলমান ডাউনলোডের URL এবং downloadId ট্র্যাক করার জন্য ম্যাপ
const pendingDownloads = new Map();

Module.onRuntimeInitialized = () => {
  evaluateUrlCpp = Module.cwrap('evaluateUrl', null, ['number', 'string']);
  addDomainCpp = Module.cwrap('addBlockedDomain', null, ['string']);
  removeDomainCpp = Module.cwrap('removeBlockedDomain', null, ['string']);

  chrome.storage.local.get(["customDomains"], (data) => {
    if (data.customDomains) {
      data.customDomains.forEach(domain => addDomainCpp(domain));
    }
  });
};

// C++ ইঞ্জিন ক্ষতিকর হিসেবে শনাক্ত করলে ট্যাব ব্লক + ডাউনলোড ক্যানসেল করবে
function handleBlockFromCpp(tabId, blockedUrl) {
  chrome.storage.local.get(["blockedCount"], (data) => {
    const newCount = (data.blockedCount || 0) + 1;
    chrome.storage.local.set({ blockedCount: newCount });
  });

  // যদি এটি কোনো ডাউনলোড লিংক হয়ে থাকে, তবে ডাউনলোডটি সাথে সাথে বাতিল ও ডিলিট করা
  if (pendingDownloads.has(blockedUrl)) {
    const downloadId = pendingDownloads.get(blockedUrl);
    chrome.downloads.cancel(downloadId, () => {
      chrome.downloads.erase({ id: downloadId });
    });
    pendingDownloads.delete(blockedUrl);
  }

  // ট্যাবটি ওয়ার্নিং পেজে রিডাইরেক্ট করা
  if (tabId && tabId > 0) {
    const blockedPageUrl = chrome.runtime.getURL("blocked.html") + `?url=${encodeURIComponent(blockedUrl)}`;
    chrome.tabs.update(tabId, { url: blockedPageUrl });
  }
}

// UI থেকে ডোমেইন অ্যাড/রিমুভ
chrome.runtime.onMessage.addListener((message) => {
  if (!addDomainCpp || !removeDomainCpp) return;
  if (message.action === "ADD_DOMAIN") addDomainCpp(message.domain);
  else if (message.action === "REMOVE_DOMAIN") removeDomainCpp(message.domain);
});

// ১. সাধারণ ওয়েবসাইট নেভিগেশন ইন্টারসেপ্ট
chrome.webNavigation.onBeforeNavigate.addListener((details) => {
  if (details.frameId !== 0 || !evaluateUrlCpp) return;
  if (!details.url.startsWith("http")) return;

  chrome.storage.local.get(["isEnabled"], (data) => {
    if (data.isEnabled === false) return;
    evaluateUrlCpp(details.tabId, details.url);
  });
});

// ২. ফাইল ডাউনলোড শুরু হওয়ার সাথে সাথে তা ইন্টারসেপ্ট করা (আপনার কোডের সম্পূর্ণ অংশ)
chrome.downloads.onCreated.addListener((downloadItem) => {
  if (!evaluateUrlCpp) return;

  chrome.storage.local.get(["isEnabled"], (data) => {
    if (data.isEnabled === false) return;

    const fileUrl = downloadItem.finalUrl || downloadItem.url;
    if (!fileUrl || !fileUrl.startsWith("http")) return;

    // ডাউনলোড আইডি সংরক্ষণ করা যাতে C++ ব্লক করলে এটি ক্যানসেল করা যায়
    pendingDownloads.set(fileUrl, downloadItem.id);

    // বর্তমান অ্যাক্টিভ ট্যাব খুঁজে বের করে C++ ইঞ্জিনে পাঠানো
    chrome.tabs.query({ active: true, currentWindow: true }, (tabs) => {
      const targetTabId = (tabs && tabs.length > 0) ? tabs[0].id : -1;
      evaluateUrlCpp(targetTabId, fileUrl);
    });
  });
});