document.addEventListener("DOMContentLoaded", () => {
  const toggleProtection = document.getElementById("toggleProtection");
  const blockedCountEl = document.getElementById("blockedCount");
  const domainInput = document.getElementById("domainInput");
  const addBtn = document.getElementById("addBtn");
  const domainList = document.getElementById("domainList");

  // সেভ করা স্টেট লোড করা
  chrome.storage.local.get(["isEnabled", "blockedCount", "customDomains"], (data) => {
    toggleProtection.checked = data.isEnabled !== false;
    blockedCountEl.textContent = data.blockedCount || 0;
    renderList(data.customDomains || []);
  });

  // প্রোটেকশন অন/অফ টগল
  toggleProtection.addEventListener("change", () => {
    chrome.storage.local.set({ isEnabled: toggleProtection.checked });
  });

  // নতুন ডোমেইন ব্লক লিস্টে যোগ করা
  addBtn.addEventListener("click", () => {
    let domain = domainInput.value.trim().toLowerCase();
    if (!domain) return;

    // ইউজার ভুল করে http:// বা https:// দিলে তা পরিষ্কার করা
    domain = domain.replace(/^https?:\/\//, '').split('/')[0];

    chrome.storage.local.get(["customDomains"], (data) => {
      const domains = data.customDomains || [];
      if (!domains.includes(domain)) {
        domains.push(domain);
        chrome.storage.local.set({ customDomains: domains }, () => {
          chrome.runtime.sendMessage({ action: "ADD_DOMAIN", domain: domain });
          renderList(domains);
          domainInput.value = "";
        });
      }
    });
  });

  // Enter চাপলেও যেন ডোমেইন অ্যাড হয়
  domainInput.addEventListener("keypress", (e) => {
    if (e.key === "Enter") addBtn.click();
  });

  function renderList(domains) {
    domainList.innerHTML = "";
    domains.forEach((domain) => {
      const li = document.createElement("li");
      li.innerHTML = `<span>${domain}</span> <span class="del-btn" data-domain="${domain}">✕</span>`;
      domainList.appendChild(li);
    });

    document.querySelectorAll(".del-btn").forEach((btn) => {
      btn.addEventListener("click", (e) => {
        const targetDomain = e.target.getAttribute("data-domain");
        chrome.storage.local.get(["customDomains"], (data) => {
          const updated = (data.customDomains || []).filter((d) => d !== targetDomain);
          chrome.storage.local.set({ customDomains: updated }, () => {
            chrome.runtime.sendMessage({ action: "REMOVE_DOMAIN", domain: targetDomain });
            renderList(updated);
          });
        });
      });
    });
  }
});