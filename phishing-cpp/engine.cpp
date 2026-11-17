#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <regex>
#include <emscripten/emscripten.h>

// এখানে আপনার Google Safe Browsing API Key বসান
const std::string API_KEY = "AIzaSyCViiJFg127NVfXBasGb8h3GdCyQbl7SVs";

// C++ মেমোরিতে ব্ল্যাকলিস্টেড ডোমেইন সেট
std::unordered_set<std::string> blockedDomains = {
    "malicious-example.com",
    "fake-login.org",
    "phishing-test.net"};

// ডোমেইন এক্সট্র্যাক্ট করার হেল্পার ফাংশন
std::string extractDomain(const std::string &url)
{
    std::string cleanUrl = url;
    size_t protocolPos = cleanUrl.find("://");
    if (protocolPos != std::string::npos)
    {
        cleanUrl = cleanUrl.substr(protocolPos + 3);
    }
    size_t slashPos = cleanUrl.find('/');
    if (slashPos != std::string::npos)
    {
        cleanUrl = cleanUrl.substr(0, slashPos);
    }
    std::transform(cleanUrl.begin(), cleanUrl.end(), cleanUrl.begin(), ::tolower);
    return cleanUrl;
}

// ব্রাউজার ট্যাব ব্লক করার ব্রিজ
EM_JS(void, triggerBrowserBlock, (int tabId, const char *blockedUrl), {
    const urlStr = UTF8ToString(blockedUrl);
    if (typeof handleBlockFromCpp === 'function')
    {
        handleBlockFromCpp(tabId, urlStr);
    }
});

// C++ থেকে সরাসরি ব্রাউজারের fetch() ব্যবহার করে Google API কল করার ব্রিজ
EM_JS(void, callGoogleApiFromCpp, (int tabId, const char *apiUrl, const char *jsonPayload, const char *originalUrl, const char *domain), {
    const endpoint = UTF8ToString(apiUrl);
    const payload = UTF8ToString(jsonPayload);
    const origUrl = UTF8ToString(originalUrl);
    const dom = UTF8ToString(domain);

    fetch(endpoint, {
        method : 'POST',
        headers : {'Content-Type' : 'application/json'},
        body : payload
    })
        .then(response => response.text())
        .then(responseText => {
            // রেসপন্স টেক্সটটি আবার C++ ফাংশনে পাঠিয়ে দেওয়া হচ্ছে বিশ্লেষণের জন্য
            Module.ccall('onApiResponseFromCpp', null, [ 'number', 'string', 'string', 'string' ], [ tabId, responseText, origUrl, dom ]);
        })
        .catch(err => console.error("Safe Browsing Fetch Error:", err));
});

// লোকাল হিউরিস্টিক ও ব্ল্যাকলিস্ট চেক
bool isLocallyMalicious(const std::string &url, const std::string &domain)
{
    if (blockedDomains.find(domain) != blockedDomains.end())
    {
        return true;
    }

    std::string lowerUrl = url;
    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);
    int riskScore = 0;

    std::regex ipPattern(R"(https?:\/\/(\d{1,3}\.){3}\d{1,3})");
    if (std::regex_search(lowerUrl, ipPattern))
        riskScore += 50;

    std::vector<std::string> keywords = {"login-verify", "update-bank", "free-crypto", "account-suspended"};
    for (const auto &word : keywords)
    {
        if (lowerUrl.find(word) != std::string::npos)
            riskScore += 40;
    }
    // সন্দেহজনক এক্সিকিউটেবল বা আর্কাইভ ফাইল ডাউনলোড চেক
    std::vector<std::string> dangerousExtensions = {".exe", ".bat", ".scr", ".vbs", ".sh", ".ps1"};
    for (const auto &ext : dangerousExtensions)
    {
        if (lowerUrl.length() >= ext.length() &&
            lowerUrl.compare(lowerUrl.length() - ext.length(), ext.length(), ext) == 0)
        {
            riskScore += 50;
        }
    }

    // কোড রিপোজিটরি (যেমন raw.githubusercontent) থেকে সরাসরি .zip ডাউনলোড হলে ব্লক
    if (lowerUrl.find("raw.githubusercontent.com") != std::string::npos &&
        lowerUrl.find(".zip") != std::string::npos)
    {
        riskScore += 50;
    }
    if (lowerUrl.find('@') != std::string::npos)
        riskScore += 40;
    if (std::count(domain.begin(), domain.end(), '.') > 3)
        riskScore += 30;

    return riskScore >= 50;
}

extern "C"
{

    // API রেসপন্স বিশ্লেষণ এবং ব্লকিং সিদ্ধান্ত (সম্পূর্ণ C++ লজিক)
    EMSCRIPTEN_KEEPALIVE
    void onApiResponseFromCpp(int tabId, const char *responseBody, const char *originalUrl, const char *domain)
    {
        std::string res(responseBody);

        // JSON রেসপন্সে "matches" থাকলে সাইটটি ক্ষতিকর
        if (res.find("\"matches\"") != std::string::npos)
        {
            blockedDomains.insert(std::string(domain));
            triggerBrowserBlock(tabId, originalUrl);
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void addBlockedDomain(const char *domain)
    {
        std::string d(domain);
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);
        blockedDomains.insert(d);
    }

    EMSCRIPTEN_KEEPALIVE
    void removeBlockedDomain(const char *domain)
    {
        std::string d(domain);
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);
        blockedDomains.erase(d);
    }

    // মেইন URL চেকিং ফাংশন
    EMSCRIPTEN_KEEPALIVE
    void evaluateUrl(int tabId, const char *rawUrl)
    {
        std::string url(rawUrl);
        std::string domain = extractDomain(url);

        // ১. প্রথমে C++ লোকাল ইঞ্জিন দিয়ে চেক
        if (isLocallyMalicious(url, domain))
        {
            triggerBrowserBlock(tabId, url.c_str());
            return;
        }

        // ২. API Key সেট করা থাকলে C++ থেকেই পেলোড বানিয়ে Google API কল
        if (API_KEY == "YOUR_GOOGLE_SAFE_BROWSING_API_KEY" || API_KEY.empty())
        {
            return;
        }

        std::string apiUrl = "https://safebrowsing.googleapis.com/v4/threatMatches:find?key=" + API_KEY;

        std::string jsonPayload = R"({
        "client": { "clientId": "phishguard-cpp", "clientVersion": "1.0.0" },
        "threatInfo": {
            "threatTypes": ["MALWARE", "SOCIAL_ENGINEERING", "UNWANTED_SOFTWARE", "POTENTIALLY_HARMFUL_APPLICATION"],
            "platformTypes": ["ANY_PLATFORM"],
            "threatEntryTypes": ["URL"],
            "threatEntries": [{ "url": ")" +
                                  url + R"(" }]
        }
    })";

        callGoogleApiFromCpp(tabId, apiUrl.c_str(), jsonPayload.c_str(), url.c_str(), domain.c_str());
    }
}