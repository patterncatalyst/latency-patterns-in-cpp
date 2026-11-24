source "https://rubygems.org"

# Exact pins, not floating ranges — re-check upstream before bumping and
# record why if a pin has to hold back from the latest stable release.
# Checked against rubygems.org 2026-11-24.
gem "jekyll", "4.4.1"
gem "kramdown-parser-gfm", "1.1.0"
# Held at 4.7.0, the latest release compatible with jekyll 4.4.1's
# `rouge >= 3.0, < 5.0` constraint; rouge 5.x is newer but not installable
# alongside this jekyll pin.
gem "rouge", "4.7.0"

group :jekyll_plugins do
  gem "jekyll-feed", "0.18.0"     # generates /feed.xml automatically
  gem "jekyll-sitemap", "1.4.0"   # generates /sitemap.xml automatically
  gem "jekyll-seo-tag", "2.9.1"   # provides {% seo %} — used in _layouts/default.html
end

# Ruby 3+ no longer bundles webrick; `jekyll serve` needs it explicitly
gem "webrick", "1.9.2"
