const params = new URLSearchParams(window.location.search);
document.getElementById('blocked-url').textContent = params.get('url') || 'Unknown URL';

document.getElementById('backBtn').addEventListener('click', () => {
  if (window.history.length > 1) {
    window.history.back();
  } else {
    window.close();
  }
});