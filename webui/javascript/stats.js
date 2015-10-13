var I2PStats = I2PStats || {}


I2PStats.Socket = function(elem) {
  this.url = "ws://" + location.host + "/stats.sock";
  this.sock = new WebSocket(this.url);
  this.packets = {};
  this.nodes = {};
  this.elem = elem;
}

function randint(mn, mx) {
  return (Math.random() * mn )+ (Math.random() * (mx - mn));
}

I2PStats.Node = function(name) {
  this.name = name
  this.rx = 0;
  this.tx = 0;
  this.cyclesQuiet = 0;
}

function nstr(str, n) {
  var s = "";
  while(n) {
    s += str;
    n--;
  }
  return s;
}

I2PStats.Node.prototype = {

  gotTx : function() {
    this.tx += 1;
  },
  
  gotRx : function() {
    this.rx += 1;
  },
  
  update : function() {
    if (this.rx || this.tx) {
      this.cyclesQuiet = 0;
    } else {
      this.cyclesQuiet ++;
    }
    // reset value tx/rx
    this.rx = this.tx = 0;
  }

  
}

function filtername(name) {
  return name;
}

function getKeyspacePosition(name) {
  if (name == "US") {
    return 0;
  }
  // TODO: implement
  return randint(0, Math.PI * 2);
}

I2PStats.Socket.prototype = {

  getNode : function(name) {
    if (!(name in this.nodes)) {
      this.nodes[name] = new I2PStats.Node(name);
      // add cy node
      var p = getKeyspacePosition(name);
      var y = (Math.sin(p) * 500 ) + (window.screen.height / 4);
      var x = (Math.cos(p) * 500 ) + (window.screen.width / 4);
      if (name === "US") {
        x = (window.screen.width / 4);
        y = (window.screen.height / 4);
      }
      this.cy.add({
        group: "nodes",
        data: { id: name },
        position: {
          y: x,
          x: y
        }
      });
    }
    return this.nodes[name];
  },

  gotI2NP : function (dst, src, msgtype, ts) {
    var dn = this.getNode(dst);
    var sn = this.getNode(src);
    var ev = src + dst;
    if (this.cy.$('edge[ id = "'+ev+'" ]').length > 0) {
      // already exists
    } else {
      console.log(src);
      console.log(dst);
      this.cy.add({
        group: "edges",
        data: {
          id: ev,
          source: src,
          target: dst,
        },
      });
    }
  },
  
  start : function() {
    var self = this;
    this.cy = cytoscape({
      layout: {
        name: 'circle',
        directed: true,
        padding: 10
      },
      container: self.elem,
      ready: function() {        
        self.sock.onmessage = function(m) {
          var j = JSON.parse(m.data);
          var msgtype = j[0];
          var src = filtername(j[1]);
          var dst = filtername(j[2]);
          var ts = j[3];
          self.gotI2NP(dst, src, msgtype, ts);
        }
      },
      style: cytoscape.stylesheet()
        .selector('node')
        .css({
          'background-color': '#B3767E',
          'width': 'mapData(baz, 0, 10, 10, 40)',
          'height': 'mapData(baz, 0, 10, 10, 40)',
          'content': 'data(id)'
        })
        .selector('edge')
        .css({
          'line-color': '#F2B1BA',
          'target-arrow-color': '#F2B1BA',
          'width': 2,
          'line-style': 'solid',
          'opacity': 1
        })
        .selector(':selected')
        .css({
          'background-color': 'black',
          'line-color': 'black',
          'target-arrow-color': 'black',
          'source-arrow-color': 'black',
          'opacity': 1
        }),
    });
    this.tick();
  },

  tick : function() {
    for ( var n in this.nodes ) {
      this.nodes[n].update();
      if ( this.nodes[n].cyclesQuiet > 10 ) {
        delete(this.nodes[n]);
        // remove cy node
        this.cy.remove('node[ id = "'+n+'" ]');
      }
    }
    var self = this;
    setTimeout(function() {
      self.tick();
    }, 1000);
  }

};
