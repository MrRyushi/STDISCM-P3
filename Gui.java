
import com.sun.net.httpserver.*;

import java.io.*;
import java.net.InetSocketAddress;
import java.nio.file.*;
import java.util.List;
import java.util.stream.Collectors;
import java.util.ArrayList;
import java.nio.charset.StandardCharsets;


public class Gui {
    private static final String VIDEO_FOLDER = "compressed_videos";

    public static void main(String[] args) throws IOException {
        int port = 5000;
        HttpServer server = HttpServer.create(new InetSocketAddress(port), 0);

        // Serve index.html dynamically
        server.createContext("/", new IndexHandler());

        // Serve video files
        server.createContext("/videos", new VideoHandler());

        server.setExecutor(null);
        server.start();
        System.out.println("Server started at http://localhost:" + port);
    }

    static class IndexHandler implements HttpHandler {
        @Override
        public void handle(HttpExchange exchange) throws IOException {
            File indexFile = new File("index.html");
            String html = new String(Files.readAllBytes(indexFile.toPath()), StandardCharsets.UTF_8);
        
            // Get video files from the directory
            File videoDir = new File("compressed_videos"); // Adjust path if needed
            List<String> videos = new ArrayList<>();
        
            if (videoDir.exists() && videoDir.isDirectory()) {
                videos = Files.list(videoDir.toPath())
                        .map(path -> path.getFileName().toString())
                        .filter(name -> name.endsWith(".mp4") || name.endsWith(".mov") || name.endsWith(".avi") || name.endsWith(".webm"))
                        .collect(Collectors.toList());
            }
        
            // Generate the video list HTML dynamically
            StringBuilder videoHtml = new StringBuilder();
            int index = 1;
            for (String video : videos) {
                videoHtml.append("<div class='video-container'>")
                         .append("<video id='vid_").append(index).append("' preload='metadata' ")
                         .append("onmouseenter='preview(this)' onmouseleave='pausePreview(this)' ")
                         .append("onclick='playFull(this)'>")
                         .append("<source src='/videos/").append(video).append("' type='video/mp4'>")
                         .append("Your browser does not support HTML5 video.</video>")
                         .append("<div class='video-name'>").append(video).append("</div>")
                         .append("</div>");
                index++;
            }
        
            // Replace the placeholder for video grid with dynamic HTML
            html = html.replace("<div class=\"video-grid\"></div>", "<div class=\"video-grid\">" + videoHtml.toString() + "</div>");
        
            // Debugging: Print final HTML to console
            //System.out.println("✅ Final HTML sent to browser:\n" + html);
        
            sendResponse(exchange, html, "text/html");
        }
    }
    
    

    // Handler for serving videos
    static class VideoHandler implements HttpHandler {
        @Override
        public void handle(HttpExchange exchange) throws IOException {
            String path = exchange.getRequestURI().getPath().replaceFirst("/videos/", "");
            File videoFile = new File(VIDEO_FOLDER, path);

            if (!videoFile.exists()) {
                System.out.println("File not found: " + videoFile.getAbsolutePath());
                exchange.sendResponseHeaders(404, -1);
                return;
            }

            String contentType = Files.probeContentType(videoFile.toPath());
            if (contentType == null) contentType = "video/mp4";

            exchange.getResponseHeaders().set("Content-Type", contentType);
            exchange.sendResponseHeaders(200, videoFile.length());

            //System.out.println("Serving video: " + videoFile.getAbsolutePath());

            OutputStream os = exchange.getResponseBody();
            Files.copy(videoFile.toPath(), os);
            os.close();
        }
    }

    // Helper function to send responses
    private static void sendResponse(HttpExchange exchange, String response, String contentType) throws IOException {
        exchange.getResponseHeaders().set("Content-Type", contentType);
        exchange.sendResponseHeaders(200, response.length());
        exchange.getResponseBody().write(response.getBytes());
        exchange.getResponseBody().close();
    }
}
