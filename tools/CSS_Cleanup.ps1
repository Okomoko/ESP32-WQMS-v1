# Find all classes in HTML
Select-String -Path ..\web_assets\*.html -Pattern 'class="([^"]*)"' -AllMatches > styles_in_html.txt

# Find all CSS definitions
Select-String -Path ..\web_assets\*.css -Pattern '\.[a-zA-Z0-9_-]+' > css_styles.txt