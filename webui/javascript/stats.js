var I2PStats = I2PStats || {}

I2PStats.Socket = function(elem) {
  this.elem = elem;
  this.url = "ws://" + location.host + "/stats.sock";
  this.sock = new WebSocket(this.url);
}

I2PStats.Socket.prototype = {
  start : function() {
    var self = this;
    this.sock.onmessage = function(m) {
      var j = JSON.parse(m.data);
      var msgtype = j[0];
      var src = j[1];
      var dst = j[2];
      if ( self.elem ) {
        var txt = "i2np type=" + msgtype + " from " + src + " to " + dst;
        var e = document.createTextNode(txt); 
        var el = document.createElement("div");
        el.appendChild(e);
        self.elem.appendChild(el);
        while (self.elem.childNodes.length > 50 ) {
          self.elem.removeChild(self.elem.firstChild);
        }
      }
    }
  }
};
