
var pcr = {
	errorMessageFadeOutTimer: null,
	errorMessageFadeOutTimerMs: 2000,
	udp: '127.0.0.1:1234',
};

pcr.update = function() {
};

pcr.startPCRFlow = function() {
	$.ajax({
		dataType: 'jsonp',
		url: '/ajax/start_flow',
		data: {udp: pcr.udp},
		success: function(ev) {
			$('#ss_button').html("Stop");
			pcr.update();
		},
		error: function(ev) {
		},
	});
};

pcr.stopPCRFlow = function() {
	$.ajax({
		dataType: 'jsonp',
		url: '/ajax/stop_flow',
		data: {udp: pcr.udp},
		success: function(ev) {
			$('#ss_button').html("Start");
		},
		error: function(ev) {
		},
	});
};

pcr.startstopPCRFlow = function(ev) {
	var btn = ev.target;
	if (btn.innerHTML == "Start") {
		pcr.startPCRFlow();
	} else {
		btn.innerHTML = "Start";
		pcr.stopPCRFlow();
	}
};

$(document).ready(function() {
	$('#error').html('pcr page loading').fadeIn('fast');
	window.clearTimeout(pcr.errorMessageFadeOutTimer);
	pcr.errorMessageFadeOutTimer = window.setTimeout(
		function() {
			$('#error').fadeOut('slow');
		},
		pcr.errorMessageFadeOutTimeoutMs
	);

	$('#ss_button').click(pcr.startstopPCRFlow);
});

