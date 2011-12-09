
var pcr = {
	errorMessageFadeOutTimer: null,
	errorMessageFadeOutTimerMs: 2000,
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

});

