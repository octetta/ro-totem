(function (global) {
    'use strict';

    const handlers = Object.create(null);

    function format(...fields) {
        return 'V1' + fields.map(field => {
            const text = encodeURIComponent(String(field ?? ''));
            return `${text.length}:${text}`;
        }).join('');
    }

    function parse(frame) {
        if (typeof frame !== 'string' || frame.slice(0, 2) !== 'V1') {
            return null;
        }
        const fields = [];
        let index = 2;
        while (index < frame.length) {
            let colon = index;
            while (colon < frame.length &&
                    frame.charCodeAt(colon) >= 48 &&
                    frame.charCodeAt(colon) <= 57) {
                colon++;
            }
            if (colon === index || frame[colon] !== ':') return null;
            const length = Number(frame.slice(index, colon));
            const start = colon + 1;
            const end = start + length;
            if (!Number.isSafeInteger(length) || length < 0 ||
                    end > frame.length) {
                return null;
            }
            try {
                fields.push(decodeURIComponent(frame.slice(start, end)));
            } catch (error) {
                return null;
            }
            index = end;
        }
        return fields;
    }

    function on(vocab, handler) {
        handlers[vocab] = handler;
    }

    function receive(frame) {
        const fields = parse(frame);
        if (!fields || fields.length < 3) return false;

        const [vocab, command, type] = fields;
        const handler = handlers[vocab];
        if (!handler) return false;
        handler(command, type, fields.slice(3));
        return true;
    }

    global.Voco = { format, parse, on, receive };
})(window);
