from flask import Flask, render_template, send_from_directory
import os

app = Flask(__name__)
VIDEO_FOLDER = './compressed_videos'

@app.route('/')
def index():
    videos = [f for f in os.listdir(VIDEO_FOLDER) if f.endswith(('.mp4', '.mov', '.avi', '.webm'))]
    return render_template('index.html', videos=videos)

@app.route('/videos/<filename>')
def serve_video(filename):
    return send_from_directory(VIDEO_FOLDER, filename)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
