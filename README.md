# forest-esp32-code
thư mục chứa code test và code up data GPS lên một server
- `firm_client`: code chính hiện tại được flash trong ROM của ESP32, lấy thông tin GPS từ nhà mạng và dùng 4G để thực hiện các HTTP request nhằm truyền data GPS cho một địa chỉ server
- `firm`: code để test GPS 
- `test`: code test khả năng chạy các HTTP(S) request
- `testest`: code để test upload code lên mạch
