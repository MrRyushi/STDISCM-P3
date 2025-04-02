// gui_server.js
const express = require('express');
const path = require('path');
const fs = require('fs');

const app = express();
const PORT = 5000;
const VIDEO_FOLDER = path.join(__dirname, 'compressed_videos');

// Serve index.html at root
app.get('/', (req, res) => {
    const videos = fs.readdirSync(VIDEO_FOLDER).filter(file => {
        return file.endsWith('.mp4') || file.endsWith('.mov') || file.endsWith('.avi') || file.endsWith('.webm');
    });

    const renderedHtml = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8')
        .replace('{% for video in videos %}', '')
        .replace(/{% endfor %}/, '')
        .replace(
            /<div class="video-grid">.*<\/div>/s,
            `<div class="video-grid">
                ${videos.map((video, i) => `
                <div class="video-container">
                    <video id="vid_${i}" preload="metadata"
                        onmouseenter="preview(this)" onmouseleave="pausePreview(this)"
                        onclick="playFull(this)">
                        <source src="/videos/${video}" type="video/mp4" />
                        Your browser does not support HTML5 video.
                    </video>
                    <div class="video-name">${video}</div>
                </div>`).join('')}
            </div>`
        );

    res.send(renderedHtml);
});

// Serve videos
app.use('/videos', express.static(VIDEO_FOLDER));

// Serve static files like index.html if needed
app.use(express.static(__dirname));

app.listen(PORT, () => {
    console.log(`Server running at http://localhost:${PORT}`);
});
