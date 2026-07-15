var express 		= require('express');
var router 			= express.Router();
var bodyParser 		= require('body-parser');
var crypto 			= require('crypto');
const tags 			= require('../controllers/tags.js');
const alarms 		= require('../controllers/alarms.js');
const active_alarms = require('../controllers/active_alarms.js');
const channels 		= require('../controllers/channels.js');
const rt_values 	= require('../controllers/rt_values.js');
const custom_view 	= require('../controllers/custom_view.js');
const write_buffer 	= require('../controllers/write_buffer.js');

var VerifyToken = require('./VerifyToken');

router.use(bodyParser.urlencoded({ extended: false }));
router.use(bodyParser.json());
//var User = require('../user/User');
const users = require('../db_apis/users.js');

/**
 * Configure JWT
 */
var jwt = require('jsonwebtoken'); // used to create, sign, and verify tokens
var bcrypt = require('bcryptjs');
var config = require('../config'); // get config file

//console.log(jwt);

router.post('/login',async  function(req, res) {
	console.log(req.body);
	//  User.findOne({ email: req.body.email }, function (err, user) {

	var user =  await users.findUser(req.body);
	if ( user)
	{

	//if (err) return res.status(500).send('Error on the server.');
	if (!user) return res.status(404).send('No user found.');
	  console.log(req.body.password);
	  console.log(user.password);
	
	var hashedpwd= crypto.createHash('sha1').update(req.body.password.toString('utf8')).digest('hex');
	 // check if the password is valid
    var passwordIsValid = (hashedpwd==user.password) ? true :false;
	var hasRight 		= (user.specialroles==3) ? true :false;
    if (!passwordIsValid || !hasRight) return res.status(401).send({ auth: false, token: null });
	
	// if user is found and password is valid
    // create a token
    var token = jwt.sign({ id: user._id }, config.secret, {
      expiresIn: 86400 // expires in 24 hours
    });
	
	    // return the information including token as JSON
    res.status(200).send({ auth: true, token: token });
	};
	

});

router.get('/logout', function(req, res) {
  res.status(200).send({ auth: false, token: null });
});

router.get('/alarms/:id?',VerifyToken, alarms.get);

router.get('/active_alarms/:id?',VerifyToken, active_alarms.get);

router.get('/tags/:id?',VerifyToken, tags.get);

router.get('/channels/:id?',VerifyToken, channels.get);

router.get('/rt_values/:id?',VerifyToken, rt_values.get);

router.get('/custom_view/:id?',VerifyToken, custom_view.get);

router.post('/write_buffer/:id?',VerifyToken, write_buffer.post);


/* router.get('/me', VerifyToken, function(req, res, next) {

  User.findById(req.userId, { password: 0 }, function (err, user) {
    if (err) return res.status(500).send("There was a problem finding the user.");
    if (!user) return res.status(404).send("No user found.");
    res.status(200).send(user);
  });

}); */

module.exports = router;