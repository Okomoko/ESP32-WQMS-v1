const http = require('http');
const express = require('express');
const webServerConfig = require('../config/web-server.js');
//const database = require('./database.js');
var basicAuth = require('basic-auth');
var crypto = require('crypto');
const morgan = require('morgan');

let httpServer;
 
function initialize() {
  return new Promise((resolve, reject) => {
    const app = express();
    httpServer = http.createServer(app);

	// Combines logging info from request and response
    app.use(morgan('combined'));
	

	app.get('/api', function (req, res) {
	  res.status(200).send('API works.');
	});

	var AuthController = require('../auth/AuthController');
	app.use('/api/auth', AuthController); 
	

 
    httpServer.listen(webServerConfig.port, err => {
      if (err) {
        reject(err);
        return;
      }
 
      console.log(`Web server listening on localhost:${webServerConfig.port}`);
 
      resolve();
    });
  });
}
 
module.exports.initialize = initialize;

function close() {
  return new Promise((resolve, reject) => {
    httpServer.close((err) => {
      if (err) {
        reject(err);
        return;
      }
 
      resolve();
    });
  });
}
 
module.exports.close = close;